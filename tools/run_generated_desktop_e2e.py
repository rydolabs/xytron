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
            feature=project/"plugins/feature/build/Release/feature.dll"
        elif sys.platform=="darwin":
            feature=project/"plugins/feature/build/libfeature.dylib"
        else:
            feature=project/"plugins/feature/build/libfeature.so"
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
        command=base+["run","--host",str(host),"--plugin",plugin_path,"--smoke"]
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
        missing=[marker for marker in markers if marker not in output]
        if rc!=0 or missing or "bridge-fail:" in output:
            raise AssertionError("Real native app E2E failed: exit={} missing={}".format(
                rc,missing))

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
