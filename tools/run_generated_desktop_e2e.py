#!/usr/bin/env python3
"""Real generated C++ app CLI -> CEF/WebView2 -> app:// JS -> C++ integration.

No mock webview; a nonzero exit, missing browser checkpoint or timeout fails.
The browser SDK and host are already built explicitly by the integration CI.
"""
import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile

def execute(argv, cwd, timeout=90):
    kwargs = {"cwd":str(cwd), "stdout":subprocess.PIPE,
              "stderr":subprocess.STDOUT, "text":True}
    if os.name == "posix":
        kwargs["start_new_session"] = True
    process = subprocess.Popen(argv,**kwargs)
    try:
        output,_ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        if os.name=="posix":
            os.killpg(process.pid,signal.SIGKILL)
        else:
            subprocess.run(["taskkill","/F","/T","/PID",str(process.pid)],
                           stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
                           check=False)
        output,_ = process.communicate()
        print(output,flush=True)
        raise AssertionError("Generated desktop runner timed out")
    print("$", " ".join(str(v) for v in argv), "\n"+output,flush=True)
    return process.returncode,output

def run():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xytron",type=Path,required=True)
    parser.add_argument("--sdk",type=Path,required=True)
    parser.add_argument("--host",type=Path,required=True)
    parser.add_argument("--engine",choices=("cef","webview2"),required=True)
    args=parser.parse_args()
    cli=args.xytron.resolve(strict=True)
    host=args.host.resolve(strict=True)
    sdk=args.sdk.resolve(strict=True)
    if args.engine=="webview2" and os.name!="nt":
        parser.error("WebView2 requires Windows")
    with tempfile.TemporaryDirectory(prefix="xytron-generated-e2e-") as tmp:
        folder=Path(tmp)
        base=[str(cli)]
        rc,output=execute(base+["new","desktop","hello","--backend","cpp"],folder)
        if rc: raise AssertionError("new desktop failed")
        project=folder/"hello"
        # Edit the actual generated user's C++ backend. A hardcoded host
        # ping stub could not pass this test; the loaded DLL/SO must reflect
        # this user source modification.
        user_backend=project/"backend"/"main.cpp"
        authored=user_backend.read_text(encoding="utf-8")
        default="pong-from-user-backend"
        custom="pong-edited-in-project"
        if authored.count(default)!=1:
            raise AssertionError("Generated C++ backend source lacks editable native method")
        user_backend.write_text(authored.replace(default,custom),encoding="utf-8")
        for argv in (
            ["restore","--sdk",str(sdk)],
            ["build"],
            ["test"],
        ):
            rc,output=execute(base+argv,project)
            if rc: raise AssertionError("generated app CLI workflow failed: "+" ".join(argv))
        # A second, user-created real C++ feature plugin must be delivered
        # into the same running WebView, using NativeWeb stable ABI v1.
        for argv in (
            ["plugin","new","feature","--language","cpp"],
            ["plugin","build","feature","--sdk",str(sdk)],
        ):
            rc,output=execute(base+argv,project)
            if rc: raise AssertionError("Feature plugin scaffold/build failed: "+" ".join(argv))
        if os.name=="nt":
            feature=project/"plugins/feature/bin/win-x64/feature.dll"
        elif sys.platform=="darwin":
            feature=project/"plugins/feature/build/libfeature.dylib"
        else:
            feature=project/"plugins/feature/bin/linux-x64/libfeature.so"
        if not feature.is_file():
            raise AssertionError("Built feature ABI plugin missing: "+str(feature))
        # Fail closed when a missing feature binary is explicitly requested.
        missing="plugins/feature/build/missing-feature.bin"
        rc,_=execute(base+["run","--host",str(host),"--plugin",missing,"--smoke"],project)
        if rc==0: raise AssertionError("Missing native feature plugin was accepted")
        # Verify invalid host paths are denied before launching anything.
        rc,_=execute(base+["run","--host",str(folder/"missing-host"),"--smoke"],
                     project)
        if rc==0: raise AssertionError("Missing executable was accepted")

        # Pass a project-relative path. CLI checks every segment for links.
        plugin_path=str(feature.relative_to(project)).replace("\\","/")
        # The CLI now discovers a locally installed host or an explicit
        # trusted host override, without requiring --host on every invocation.
        os.environ["XYTRON_DESKTOP_HOST"]=str(host)
        rc,diagnostic=execute(base+["doctor","--runtime"],project)
        if rc or "Desktop runtime preflight PASS:" not in diagnostic:
            raise AssertionError("Installed real browser host runtime preflight failed")
        os.environ["XYTRON_DESKTOP_HOST"]=str(folder/"missing-engine-host")
        try:
            rc,_=execute(base+["doctor","--runtime"],project)
            if rc==0: raise AssertionError("Missing engine host passed runtime preflight")
        finally:
            os.environ["XYTRON_DESKTOP_HOST"]=str(host)
        command=base+["run","--plugin",plugin_path,"--smoke"]
        if args.engine=="cef" and sys.platform.startswith("linux") and not os.getenv("DISPLAY"):
            if not shutil.which("xvfb-run"):
                raise AssertionError("Real CEF smoke requires DISPLAY or xvfb-run")
            command=["xvfb-run","-a"]+command
        rc,output=execute(command,project,timeout=140)
        markers=(
            "checkpoint: native-app-window-created",
            "checkpoint: native-app-ui-loaded app://desktop/index.html",
            "checkpoint: generated-app-js-native-pong",
            "checkpoint: user-cpp-backend-and-feature-plugin",
            "checkpoint: plugin-native-js-event-unsubscribe",
            "checkpoint: feature-plugin-unloaded",
            "Real browser app:// host completed successfully."
        )
        if args.engine=="webview2":
            # The app:// page must have received a native WM_COMMAND-driven
            # WebView::emit via JS onMessage. Count must be exact to prove
            # both the delivery and the unsubscribed second notification.
            markers+=(
                "checkpoint: win32-menu-js-subscriber-action-unsubscribe",
                "checkpoint: win32-context-js-subscriber-action-unsubscribe",
                "checkpoint: win32-exit-js-subscriber",
            )
            for native_marker in (
                "checkpoint: native-win32-menu-command-action",
                "checkpoint: native-win32-context-command-action",
            ):
                if output.count(native_marker)!=2:
                    raise AssertionError("Expected two real Win32 menu commands for "+
                                         native_marker+", got "+
                                         str(output.count(native_marker)))
            if output.count("checkpoint: native-win32-exit-command")!=1:
                raise AssertionError("Native Exit command was not dispatched exactly once")
        missing=[marker for marker in markers if marker not in output]
        if rc!=0 or missing or "bridge-fail:" in output:
            raise AssertionError("Real native app E2E failed: exit={} missing={}".format(
                rc,missing))

        # Windows-only local staging vertical slice: publish the compiled
        # first-party WebView2 host, user DLL, UI and native feature. Run from
        # the output folder after hiding all corresponding project sources.
        # No CEF binaries or commercial runtime redistributables are copied.
        if args.engine=="webview2":
            for unsafe in ("../outside", "dist/../outside", "dist/missing?name"):
                rc,_=execute(base+["publish","--output",unsafe],project)
                if rc==0: raise AssertionError("Unsafe local publication path accepted")
            rc,published=execute(base+["publish","--output","dist/hello"],project)
            if rc or "Windows WebView2 local app staging:" not in published:
                raise AssertionError("Portable local WebView2 staging failed")
            bundle=project/"dist/hello"
            launcher=bundle/"launch.cmd"
            staged_host=bundle/"xytron_desktop_host.exe"
            staged_backend=bundle/"bin/xytron_app_backend.dll"
            staged_feature=bundle/"bin/feature.dll"
            built_backend=(project/".xytron-build/out/Release/xytron_app_backend.dll")
            if not built_backend.is_file():
                built_backend=project/".xytron-build/out/xytron_app_backend.dll"
            if (not launcher.is_file() or staged_host.read_bytes()!=host.read_bytes()
                or staged_backend.read_bytes()!=built_backend.read_bytes()
                or staged_feature.read_bytes()!=feature.read_bytes()
                or (bundle/"ui/index.html").read_bytes()!=
                   (project/"ui/index.html").read_bytes()):
                raise AssertionError("Published local bundle source bytes mismatch")
            rc,_=execute(base+["publish","--output","dist/hello"],project)
            if rc==0: raise AssertionError("Existing published bundle overwritten")
            hidden=[]
            try:
                for original in (project/"ui/index.html", built_backend, feature):
                    backup=original.with_name(original.name+".source-hidden")
                    original.rename(backup)
                    hidden.append((original,backup))
                command=["cmd.exe","/d","/c",str(launcher),"--smoke"]
                rc,portable_output=execute(command,bundle,timeout=140)
                for marker in (
                    "checkpoint: user-cpp-backend-and-feature-plugin",
                    "checkpoint: plugin-native-js-event-unsubscribe",
                    "checkpoint: win32-menu-js-subscriber-action-unsubscribe",
                    "checkpoint: win32-context-js-subscriber-action-unsubscribe",
                    "checkpoint: win32-exit-js-subscriber",
                    "xytron native desktop host: PASS",
                ):
                    if marker not in portable_output:
                        raise AssertionError("Detached WebView2 bundle missing "+marker)
                if rc: raise AssertionError("Detached local bundle exit="+str(rc))
                print("Windows portable local C++ WebView2 staging smoke: PASS",
                      flush=True)
            finally:
                for original,backup in reversed(hidden):
                    backup.rename(original)

        # The host must never display an arbitrary external UI if app
        # validation is broken. Remove generated UI, then verify denial.
        ui=project/"ui"/"index.html"
        backup=ui.with_suffix(".saved")
        ui.rename(backup)
        try:
            rc,_=execute(base+["run","--host",str(host),"--smoke"],project)
            if rc==0: raise AssertionError("Missing application UI accepted")
        finally:
            backup.rename(ui)
        # Real managed Main is user source, not a hardcoded host ping.
        # Both workers speak the existing bounded private JSONL protocol.
        for language, source, before, after in (
            ("python","backend/main.py",
             "pong-from-user-python","pong-edited-in-project"),
            ("csharp","backend/Program.cs",
             "pong-from-user-csharp","pong-edited-in-project"),
        ):
            name="hello_"+language
            rc,_=execute(base+["new","desktop",name,"--backend",language],folder)
            if rc: raise AssertionError("Cannot scaffold "+language+" app")
            app=folder/name
            authored_file=app/source
            authored=authored_file.read_text(encoding="utf-8")
            if authored.count(before)!=1:
                raise AssertionError("Generated "+language+" method is not editable")
            authored=authored.replace(before,after)
            if language=="python":
                # User-added exports, not generated defaults, prove discovery.
                authored += """
    @export
    def add(self, a: int, b: int) -> int:
        return a + b

    @export
    def invert(self, value: bool) -> bool:
        return not value

    @export
    def echo(self, message: str) -> str:
        return message

    @export
    def nothing(self):
        return None

    @property
    @export
    def count(self):
        return getattr(self, "_count", 0)

    @count.setter
    def count(self, value: int):
        self._count = value

    def secret(self):
        return "NOT_EXPORTED"
"""
            else:
                token="}\ninternal static class Program"
                if authored.count(token)!=1:
                    raise AssertionError("Missing Main class insertion point")
                authored=authored.replace(token,"""
    [Export] public int add(int a, int b) => a + b;
    [Export] public bool invert(bool value) => !value;
    [Export] public string echo(string message) => message;
    [Export] public string? nothing() => null;
    [Export] public int count { get; set; }
    public string secret() => "NOT_EXPORTED";
}
internal static class Program""")
            authored_file.write_text(authored,encoding="utf-8")
            for step in (["restore","--sdk",str(sdk)], ["build"], ["test"]):
                rc,_=execute(base+step,app)
                if rc: raise AssertionError(language+" app "+step[0]+" failed")
            rc,diagnostic=execute(base+["doctor","--runtime"],app)
            if rc or "Desktop runtime preflight PASS:" not in diagnostic:
                raise AssertionError(language+" real runtime doctor failed")
            grants=app/".xytron-build/managed_operations.txt"
            operations=grants.read_text(encoding="utf-8").splitlines()
            expected={
                "app.Main.create", "app.Main.dispose", "app.Main.method.ping",
                "app.Main.method.add", "app.Main.method.invert",
                "app.Main.method.echo", "app.Main.method.nothing",
                "app.Main.get.count", "app.Main.set.count",
            }
            if set(operations)!=expected or len(operations)!=len(expected):
                raise AssertionError(language+" discovered export mismatch: "+repr(operations))
            if "app.Main.method.secret" in operations:
                raise AssertionError("Private method was exposed")
            # A modified or stale manifest cannot be used to bypass the
            # native host's finite/unique operation check.
            grant_backup=grants.read_bytes()
            grants.write_bytes(grant_backup+b"app.Main.method.secret\napp.Main.method.secret\n")
            try:
                rc,_=execute(base+["run","--host",str(host),"--smoke"],app)
                if rc==0: raise AssertionError("Duplicate managed grant accepted")
            finally:
                grants.write_bytes(grant_backup)
            # A missing worker / assembly must fail closed before browser use.
            if language=="csharp":
                required=app/".xytron-build/dotnet/MainBackend.dll"
            else:
                required=app/"backend/main.py"
            backup=required.with_name(required.name+".bak")
            required.rename(backup)
            try:
                rc,_=execute(base+["doctor","--runtime"],app)
                if rc==0: raise AssertionError(language+" missing Main passed doctor")
                rc,_=execute(base+["run","--host",str(host),"--smoke"],app)
                if rc==0: raise AssertionError(language+" missing Main accepted")
            finally:
                backup.rename(required)
            args_managed=base+["run","--host",str(host),"--smoke"]
            if args.engine=="cef" and sys.platform.startswith("linux") and not os.getenv("DISPLAY"):
                args_managed=["xvfb-run","-a"]+args_managed
            rc,output=execute(args_managed,app,timeout=140)
            checkpoints=[
                "checkpoint: native-app-window-created",
                "checkpoint: native-app-ui-loaded app://desktop/index.html",
                "checkpoint: managed-main-worker-granted-"+language,
                "checkpoint: user-managed-main-js-verified-"+language,
                "Real browser app:// host completed successfully."
            ]
            missing=[item for item in checkpoints if item not in output]
            if rc or missing or "bridge-fail:" in output:
                raise AssertionError(language+" actual browser worker failed: "+
                                     str((rc,missing)))
            print("Generated {} Main real {} JS E2E: PASS".format(
                language,args.engine),flush=True)

        print("Generated app real {} browser E2E: PASS".format(args.engine),
              flush=True)
    return 0

if __name__=="__main__":
    try:
        sys.exit(run())
    except (AssertionError, OSError, subprocess.SubprocessError) as exc:
        print("Generated app real-browser E2E FAILED:",exc,file=sys.stderr)
        sys.exit(1)
