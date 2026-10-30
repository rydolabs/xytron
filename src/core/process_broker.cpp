#include "core/process_broker.hpp"
#include "core/json_codec.hpp"
#include "nativeweb/error.hpp"

#include <chrono>
#include <cctype>
#include <climits>
#include <cmath>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace nativeweb { namespace detail {
namespace {
const std::size_t kMaxFrame = 65536;
const std::size_t kMaxArguments = 32;

void protocolFailure() {
    throw Error("worker_protocol", "Managed worker protocol violation");
}
void workerFailure() {
    throw Error("worker_unavailable", "Managed worker unavailable or timed out");
}
void validateScalar(const Any& value) {
    if (value.empty()) return;
    if (value.type()==typeid(bool) || value.type()==typeid(int) || value.type()==typeid(std::string)) return;
    if (value.type()==typeid(double) && std::isfinite(AnyCast<double>(value))) return;
    throw Error("worker_argument", "Managed IPC only supports finite JSON scalar arguments");
}
void validateCommand(const std::vector<std::string>& cmd) {
    if (cmd.empty() || cmd.size()>32 || cmd[0].empty())
        throw Error("worker_launch", "Empty or oversized worker command");
#if defined(_WIN32)
    const bool absolute = (cmd[0].size()>2 && std::isalpha(static_cast<unsigned char>(cmd[0][0])) &&
        cmd[0][1]==':' && (cmd[0][2]=='/' || cmd[0][2]=='\\')) ||
        (cmd[0].size()>3 && cmd[0][0]=='\\' && cmd[0][1]=='\\');
#else
    const bool absolute = cmd[0][0]=='/';
#endif
    if (!absolute) throw Error("worker_launch", "Worker executable must be an absolute trusted path");
    for (std::size_t i=0; i<cmd.size(); ++i)
        if (cmd[i].size()>4096 || cmd[i].find('\0')!=std::string::npos)
            throw Error("worker_launch", "Invalid worker command argument");
}
}

struct ProcessBroker::Process {
#if defined(_WIN32)
    HANDLE input;
    HANDLE output;
    HANDLE handle;
    Process():input(NULL),output(NULL),handle(NULL){}
#else
    int input;
    int output;
    pid_t pid;
    Process():input(-1),output(-1),pid(-1){}
#endif
};

#if defined(_WIN32)
namespace {
void closeHandle(HANDLE& h){if(h && h!=INVALID_HANDLE_VALUE){CloseHandle(h);h=NULL;}}
std::wstring toWide(const std::string& s) {
    if (s.empty()) return L"";
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),NULL,0);
    if (!n) throw Error("worker_launch","Invalid UTF-8 worker command");
    std::wstring w(n,L'\0');
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),&w[0],n))
        throw Error("worker_launch","Invalid UTF-8 worker command");
    return w;
}
// Own the write buffer until the synchronous Windows pipe writer exits, even
// if its parent call timed out. Anonymous pipes do not support reliable
// overlapped writes; cancel the blocked thread and terminate the child.
struct PipeWriteState {
    HANDLE pipe;
    std::string frame;
    DWORD written;
    BOOL success;
    PipeWriteState(HANDLE p, const std::string& f)
        : pipe(p), frame(f), written(0), success(FALSE) {}
};
struct PipeWriteJob {
    std::shared_ptr<PipeWriteState> state;
    explicit PipeWriteJob(const std::shared_ptr<PipeWriteState>& s) : state(s) {}
};
DWORD WINAPI pipeWriteThread(LPVOID argument) {
    std::unique_ptr<PipeWriteJob> job(static_cast<PipeWriteJob*>(argument));
    const std::shared_ptr<PipeWriteState> state=job->state;
    state->success=WriteFile(state->pipe,state->frame.data(),
        static_cast<DWORD>(state->frame.size()),&state->written,NULL);
    return 0;
}
std::wstring quote(const std::wstring& s){
    std::wstring out=L"\"";
    unsigned backslashes=0;
    for(wchar_t c:s){
        if(c==L'\\'){++backslashes;continue;}
        if(c==L'\"'){out.append(backslashes*2+1,L'\\');out+=c;backslashes=0;continue;}
        out.append(backslashes,L'\\');backslashes=0;out+=c;
    }
    out.append(backslashes*2,L'\\');out+=L'\"';return out;
}
}
#else
namespace {
void closeFd(int& fd){if(fd>=0){close(fd);fd=-1;}}
class BlockSigpipe {
public:
    BlockSigpipe():blocked_(false),wasPending_(false){
        sigemptyset(&set_);sigaddset(&set_,SIGPIPE);
        sigset_t p; if(pthread_sigmask(SIG_BLOCK,&set_,&old_)!=0)return;
        blocked_=true;
        if(sigpending(&p)==0)wasPending_=sigismember(&p,SIGPIPE)!=0;
    }
    ~BlockSigpipe(){
        if(!blocked_)return;
        sigset_t p;
        if(!wasPending_ && sigpending(&p)==0 && sigismember(&p,SIGPIPE)){
#if defined(__APPLE__)
            // Darwin does not expose sigtimedwait(). sigpending() above
            // guarantees SIGPIPE is already queued and blocked on this
            // thread, so sigwait() immediately drains only the new signal.
            int received=0;
            (void)sigwait(&set_,&received);
#else
            struct timespec t={0,0};(void)sigtimedwait(&set_,NULL,&t);
#endif
        }
        pthread_sigmask(SIG_SETMASK,&old_,NULL);
    }
private:
    bool blocked_,wasPending_;
    sigset_t set_,old_;
};
}
#endif

ProcessBroker::ProcessBroker(const std::vector<std::string>& cmd,unsigned timeoutMs)
    : process_(new Process()),timeoutMs_(timeoutMs),nextId_(1),stopped_(false) {
    validateCommand(cmd);
    if(timeoutMs<10 || timeoutMs>60000)throw Error("worker_launch","Invalid timeout");
#if defined(_WIN32)
    SECURITY_ATTRIBUTES security={sizeof(SECURITY_ATTRIBUTES),NULL,TRUE};
    HANDLE childIn=NULL,parentIn=NULL,parentOut=NULL,childOut=NULL;
    if(!CreatePipe(&childIn,&parentIn,&security,0)) workerFailure();
    if(!SetHandleInformation(parentIn,HANDLE_FLAG_INHERIT,0)){closeHandle(childIn);closeHandle(parentIn);workerFailure();}
    if(!CreatePipe(&parentOut,&childOut,&security,0)) {closeHandle(childIn);closeHandle(parentIn);workerFailure();}
    if(!SetHandleInformation(parentOut,HANDLE_FLAG_INHERIT,0)) {
        closeHandle(childIn);closeHandle(parentIn);closeHandle(parentOut);closeHandle(childOut);workerFailure();
    }
    // STARTF_USESTDHANDLES with no standard output sharing beyond private pipes.
    STARTUPINFOW si={};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;
    si.hStdInput=childIn;si.hStdOutput=childOut;
    si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    std::wstring command;
    for(std::size_t i=0;i<cmd.size();++i){if(i)command+=L' ';command+=quote(toWide(cmd[i]));}
    std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(0);
    PROCESS_INFORMATION pi={};
    // Executable specified separately to prevent quoted command name substitution.
    const std::wstring executable=toWide(cmd[0]);
    BOOL ok=CreateProcessW(executable.c_str(),&buffer[0],NULL,NULL,TRUE,
        CREATE_NO_WINDOW,NULL,NULL,&si,&pi);
    closeHandle(childIn);closeHandle(childOut);
    if(!ok){closeHandle(parentIn);closeHandle(parentOut);workerFailure();}
    CloseHandle(pi.hThread);
    process_->handle=pi.hProcess;process_->input=parentIn;process_->output=parentOut;
#else
    int in[2]={-1,-1},out[2]={-1,-1};
    if(pipe(in)!=0)workerFailure();
    if(pipe(out)!=0){closeFd(in[0]);closeFd(in[1]);workerFailure();}
    std::vector<char*> args;
    for(std::size_t i=0;i<cmd.size();++i)args.push_back(const_cast<char*>(cmd[i].c_str()));
    args.push_back(NULL);
    pid_t child=fork();
    if(child<0){closeFd(in[0]);closeFd(in[1]);closeFd(out[0]);closeFd(out[1]);workerFailure();}
    if(child==0){
        (void)dup2(in[0],STDIN_FILENO);(void)dup2(out[1],STDOUT_FILENO);
        close(in[0]);close(in[1]);close(out[0]);close(out[1]);
        execvp(args[0],&args[0]);_exit(127);
    }
    closeFd(in[0]);closeFd(out[1]);
    process_->input=in[1];process_->output=out[0];process_->pid=child;
    // poll(POLLOUT) on a blocking pipe is insufficient for a large write:
    // write() may still block after filling the buffer. Enforce O_NONBLOCK.
    const int flags=fcntl(process_->input,F_GETFL,0);
    if(flags<0 || fcntl(process_->input,F_SETFL,flags|O_NONBLOCK)<0) {
        stopLocked(); workerFailure();
    }
#endif
}

ProcessBroker::~ProcessBroker(){stop();}

void ProcessBroker::stopLocked(){
    if(stopped_)return;
    stopped_=true;
#if defined(_WIN32)
    if(process_){
        closeHandle(process_->input);closeHandle(process_->output);
        if(process_->handle){
            if(WaitForSingleObject(process_->handle,100)!=WAIT_OBJECT_0)
                TerminateProcess(process_->handle,1);
            (void)WaitForSingleObject(process_->handle,2000);
            closeHandle(process_->handle);
        }
    }
#else
    if(process_){
        closeFd(process_->input);closeFd(process_->output);
        if(process_->pid>0){
            pid_t p=waitpid(process_->pid,NULL,WNOHANG);
            if(p==0){kill(process_->pid,SIGTERM);
                for(int i=0;i<30;++i){p=waitpid(process_->pid,NULL,WNOHANG);if(p!=0)break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
                if(p==0){kill(process_->pid,SIGKILL);(void)waitpid(process_->pid,NULL,0);}
            }
            process_->pid=-1;
        }
    }
#endif
}
void ProcessBroker::stop(){std::lock_guard<std::mutex> lock(mutex_);stopLocked();}
bool ProcessBroker::running() const {std::lock_guard<std::mutex> lock(mutex_);return !stopped_;}

std::string ProcessBroker::exchange(const std::string& frame){
    using Clock=std::chrono::steady_clock;
    const Clock::time_point deadline=Clock::now()+std::chrono::milliseconds(timeoutMs_);
#if defined(_WIN32)
    // WriteFile on a Windows anonymous pipe can block indefinitely if the
    // child never drains stdin. Enforce the same request deadline as reads.
    const std::shared_ptr<PipeWriteState> state(
        new PipeWriteState(process_->input,frame));
    PipeWriteJob* job=new PipeWriteJob(state);
    HANDLE writer=CreateThread(NULL,0,&pipeWriteThread,job,0,NULL);
    if(!writer){delete job;workerFailure();}
    const Clock::time_point now=Clock::now();
    const std::chrono::milliseconds left=
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline-now);
    const DWORD limit=left.count()>0 ? static_cast<DWORD>(left.count()) : 0;
    const DWORD waited=WaitForSingleObject(writer,limit);
    if(waited!=WAIT_OBJECT_0){
        // Cancellation may take time; terminating the private worker ensures
        // its pipe reader disappears. The thread retains the shared buffer.
        (void)CancelSynchronousIo(writer);
        if(process_->handle)(void)TerminateProcess(process_->handle,1);
        (void)WaitForSingleObject(writer,500);
        CloseHandle(writer);
        workerFailure();
    }
    CloseHandle(writer);
    if(!state->success || state->written!=frame.size())workerFailure();
    std::string answer;
    for(;;){
        if(Clock::now()>=deadline)workerFailure();
        DWORD available=0;
        if(!PeekNamedPipe(process_->output,NULL,0,NULL,&available,NULL))workerFailure();
        if(available){
            char tmp[1024];DWORD got=0;
            DWORD take=available<sizeof(tmp)?available:sizeof(tmp);
            if(!ReadFile(process_->output,tmp,take,&got,NULL)||!got)workerFailure();
            answer.append(tmp,got);
            if(answer.size()>kMaxFrame+1)protocolFailure();
            std::size_t lf=answer.find('\n');
            if(lf!=std::string::npos){if(lf+1!=answer.size())protocolFailure();answer.resize(lf);return answer;}
        }else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
#else
    BlockSigpipe sigpipe;
    std::size_t index=0;
    while(index<frame.size()){
        int left=static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count());
        if(left<=0)workerFailure();
        struct pollfd p={process_->input,POLLOUT,0};
        int rc=poll(&p,1,left);
        if(rc<=0 || (p.revents&(POLLERR|POLLHUP|POLLNVAL)))workerFailure();
        ssize_t n=write(process_->input,frame.data()+index,frame.size()-index);
        if(n<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK))continue;
        if(n<=0)workerFailure();
        index+=static_cast<std::size_t>(n);
    }
    std::string answer;
    for(;;){
        int left=static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count());
        if(left<=0)workerFailure();
        struct pollfd p={process_->output,POLLIN,0};
        int rc=poll(&p,1,left);
        if(rc<=0 || !(p.revents&POLLIN))workerFailure();
        char tmp[512];ssize_t n=read(process_->output,tmp,sizeof(tmp));
        if(n<0 && errno==EINTR)continue;
        if(n<=0)workerFailure();
        answer.append(tmp,static_cast<std::size_t>(n));
        if(answer.size()>kMaxFrame+1)protocolFailure();
        std::size_t lf=answer.find('\n');
        if(lf!=std::string::npos){if(lf+1!=answer.size())protocolFailure();answer.resize(lf);return answer;}
    }
#endif
}

Any ProcessBroker::invoke(const std::string& operation,const VariantList& args){
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopped_)throw Error("worker_stopped","Managed worker already stopped");
    if(operation.empty()||operation.size()>256||operation.find('\0')!=std::string::npos||args.size()>kMaxArguments)
        throw Error("worker_argument","Invalid operation or too many arguments");
    for(std::size_t i=0;i<args.size();++i)validateScalar(args[i]);
    if(nextId_==INT_MAX){stopLocked();throw Error("worker_protocol","Request ID sequence exhausted");}
    const int id=nextId_++;
    VariantDict request;request["id"]=id;request["operation"]=operation;request["args"]=args;
    const std::string data=anyToJson(Any(request))+"\n";
    if(data.size()>kMaxFrame)throw Error("worker_argument","Request frame too large");
    try {
        const std::string reply=exchange(data);
        if(reply.size()>kMaxFrame)protocolFailure();
        const Any parsed=jsonToAny(reply);
        if(parsed.type()!=typeid(VariantDict))protocolFailure();
        const VariantDict response=AnyCast<VariantDict>(parsed);
        VariantDict::const_iterator idIt=response.find("id"),okIt=response.find("ok");
        if(idIt==response.end()||idIt->second.type()!=typeid(int)||AnyCast<int>(idIt->second)!=id||
           okIt==response.end()||okIt->second.type()!=typeid(bool))protocolFailure();
        if(AnyCast<bool>(okIt->second)){
            if(response.size()!=3||response.find("result")==response.end())protocolFailure();
            const Any result=response.find("result")->second;
            try { validateScalar(result); }
            catch (const Error&) { protocolFailure(); }
            return result;
        }
        if(response.size()!=3||response.find("error")==response.end()||
           response.find("error")->second.type()!=typeid(std::string))protocolFailure();
        // Never propagate untrusted child-supplied text into browser exceptions.
        throw Error("worker_rejected","Managed worker rejected the operation");
    }catch(const Error& e){
        if(e.code()!="worker_rejected")stopLocked();
        throw;
    }catch(...){stopLocked();protocolFailure();}
    throw Error("worker_protocol", "Unreachable protocol state");
}

}} // namespace nativeweb::detail
