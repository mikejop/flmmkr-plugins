#!/usr/bin/env python3
"""FLMMKR FALSE COLOR - instalador grafico (macOS 13+, Windows 10+, Linux desktop).

Abre uma janela, encontra o DaVinci Resolve e instala o bundle OFX. Se o Resolve nao for encontrado,
o usuario aponta a pasta onde ele esta e o instalador escaneia essa pasta.

Modos sem janela (testes/automacao):
  --cli [--dest PASTA] [--dry-run]   instala sem GUI
  --find PASTA                       so escaneia PASTA e mostra o que achou
  --uninstall --cli                  remove o plugin
"""
import os
import platform
import shlex
import shutil
import subprocess
import sys

APP_NAME = "FLMMKR FALSE COLOR"
BUNDLE_NAME = "FalseColor.ofx.bundle"
IS_MAC = sys.platform == "darwin"
IS_WIN = sys.platform.startswith("win")
IS_LINUX = sys.platform.startswith("linux")


# ----------------------------------------------------------------------------- payload
def payload_dir():
    """Pasta que contem FalseColor.ofx.bundle (dentro do app empacotado ou ao lado do script)."""
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    for cand in (os.path.join(base, "payload"), os.path.join(os.path.dirname(os.path.abspath(__file__)), "payload")):
        if os.path.isdir(os.path.join(cand, BUNDLE_NAME)):
            return cand
    return None


def bundle_has_binary_for_this_os(bundle):
    sub = "MacOS" if IS_MAC else ("Win64" if IS_WIN else "Linux-x86-64")
    d = os.path.join(bundle, "Contents", sub)
    return os.path.isdir(d) and any(f.endswith(".ofx") for f in os.listdir(d))


# ----------------------------------------------------------------------------- requisitos de SO
def check_os():
    """Retorna None se ok, ou mensagem de erro."""
    if IS_MAC:
        try:
            major = int(platform.mac_ver()[0].split(".")[0] or 0)
        except ValueError:
            major = 0
        if major and major < 13:
            return "Este instalador exige macOS 13 ou superior."
    elif IS_WIN:
        if sys.getwindowsversion().major < 10:
            return "Este instalador exige Windows 10 ou superior."
    elif not IS_LINUX:
        return "Sistema operacional nao suportado."
    return None


# ----------------------------------------------------------------------------- deteccao
def standard_ofx_dir():
    if IS_MAC:
        return "/Library/OFX/Plugins"
    if IS_WIN:
        return os.path.join(os.environ.get("CommonProgramFiles", r"C:\Program Files\Common Files"), "OFX", "Plugins")
    return "/usr/OFX/Plugins"


def default_resolve_candidates():
    if IS_MAC:
        return ["/Applications/DaVinci Resolve/DaVinci Resolve.app", "/Applications/DaVinci Resolve.app"]
    if IS_WIN:
        pf = [os.environ.get("ProgramFiles", r"C:\Program Files"), os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")]
        return [os.path.join(p, "Blackmagic Design", "DaVinci Resolve", "Resolve.exe") for p in pf]
    return ["/opt/resolve/bin/resolve", "/opt/resolve", os.path.expanduser("~/resolve/bin/resolve")]


RESOLVE_NAMES = {"resolve", "resolve.exe", "davinci resolve.app", "resolve.app", "davinciresolve"}


def detect_resolve():
    for c in default_resolve_candidates():
        if os.path.exists(c):
            return c
    return None


def scan_for_resolve(root, max_depth=6):
    """Escaneia `root` procurando o executavel do Resolve e pastas OFX/Plugins ja existentes.
    Retorna dict(resolve=[caminhos], ofx_dirs=[caminhos])."""
    found = {"resolve": [], "ofx_dirs": []}
    root = os.path.abspath(root)
    if not os.path.isdir(root):
        return found
    base_depth = root.rstrip(os.sep).count(os.sep)
    for dirpath, dirnames, filenames in os.walk(root, followlinks=False):
        depth = dirpath.rstrip(os.sep).count(os.sep) - base_depth
        if depth >= max_depth:
            dirnames[:] = []
        for d in list(dirnames):
            full = os.path.join(dirpath, d)
            if d.lower().endswith(".app"):
                if d.lower() in RESOLVE_NAMES:
                    found["resolve"].append(full)
                dirnames.remove(d)  # nunca entra dentro de um .app (evita Project Server etc.)
            elif d.lower() == "plugins" and os.path.basename(dirpath).lower() == "ofx":
                found["ofx_dirs"].append(full)
        for f in filenames:
            if f.lower() in ("resolve", "resolve.exe"):
                found["resolve"].append(os.path.join(dirpath, f))
    return found


def choose_destination(scan=None):
    """Decide a pasta OFX. Se o scan achou OFX/Plugins, usa; senao a pasta padrao do sistema
    (onde o Resolve carrega plugins OFX)."""
    if scan and scan["ofx_dirs"]:
        return scan["ofx_dirs"][0]
    return standard_ofx_dir()


# ----------------------------------------------------------------------------- instalacao
class InstallError(Exception):
    pass


def _writable(dest):
    probe = dest
    while probe and not os.path.exists(probe):
        probe = os.path.dirname(probe)
    return bool(probe) and os.access(probe, os.W_OK)


def _install_shell_cmd(src_bundle, dest_dir):
    tgt = os.path.join(dest_dir, BUNDLE_NAME)
    q = shlex.quote
    cmd = f"mkdir -p {q(dest_dir)} && rm -rf {q(tgt)} && cp -R {q(src_bundle)} {q(dest_dir)}/"
    if IS_MAC:
        cmd += f" && (xattr -dr com.apple.quarantine {q(tgt)} || true) && (codesign --force --deep --sign - {q(tgt)} || true)"
    else:
        cmd += f" && chmod -R a+rX {q(tgt)}"
    return cmd


def _remove_shell_cmd(dest_dir):
    return f"rm -rf {shlex.quote(os.path.join(dest_dir, BUNDLE_NAME))}"


def _run_elevated(shell_cmd):
    if IS_MAC:
        esc = shell_cmd.replace("\\", "\\\\").replace('"', '\\"')
        script = f'do shell script "{esc}" with administrator privileges'
        r = subprocess.run(["osascript", "-e", script], capture_output=True, text=True)
    elif IS_LINUX:
        if not shutil.which("pkexec"):
            raise InstallError("pkexec nao encontrado. Rode manualmente no terminal:\n  sudo sh -c " + shlex.quote(shell_cmd))
        r = subprocess.run(["pkexec", "/bin/sh", "-c", shell_cmd], capture_output=True, text=True)
    else:
        raise InstallError("Elevacao nao suportada neste SO (execute o instalador como administrador).")
    if r.returncode != 0:
        msg = (r.stderr or r.stdout or "").strip()
        if "User canceled" in msg or r.returncode in (126, 127) and IS_LINUX:
            raise InstallError("Instalacao cancelada (permissao de administrador nao concedida).")
        raise InstallError("Falha ao instalar com privilegios de administrador:\n" + msg)


def install(dest_dir, dry_run=False):
    err = check_os()
    if err:
        raise InstallError(err)
    pdir = payload_dir()
    if not pdir:
        raise InstallError("Plugin nao encontrado dentro do instalador (pasta payload/%s ausente)." % BUNDLE_NAME)
    src = os.path.join(pdir, BUNDLE_NAME)
    if not bundle_has_binary_for_this_os(src):
        raise InstallError("Este instalador nao contem o binario do plugin para o seu sistema.")
    if dry_run:
        return os.path.join(dest_dir, BUNDLE_NAME)
    if IS_WIN:
        tgt = os.path.join(dest_dir, BUNDLE_NAME)
        try:
            os.makedirs(dest_dir, exist_ok=True)
            shutil.rmtree(tgt, ignore_errors=True)
            shutil.copytree(src, tgt)
        except PermissionError:
            raise InstallError("Sem permissao. Execute o instalador como administrador.")
    elif _writable(dest_dir) and os.geteuid() == 0:
        subprocess.run(["/bin/sh", "-c", _install_shell_cmd(src, dest_dir)], check=True)
    elif _writable(dest_dir):
        tgt = os.path.join(dest_dir, BUNDLE_NAME)
        os.makedirs(dest_dir, exist_ok=True)
        shutil.rmtree(tgt, ignore_errors=True)
        shutil.copytree(src, tgt, symlinks=True)
    else:
        _run_elevated(_install_shell_cmd(src, dest_dir))
    final = os.path.join(dest_dir, BUNDLE_NAME)
    if not os.path.isdir(final):
        raise InstallError("A copia terminou mas o plugin nao esta em " + final)
    return final


def uninstall(dest_dir):
    tgt = os.path.join(dest_dir, BUNDLE_NAME)
    if not os.path.exists(tgt):
        return False
    if IS_WIN or _writable(os.path.dirname(tgt)) and os.access(tgt, os.W_OK):
        shutil.rmtree(tgt)
    else:
        _run_elevated(_remove_shell_cmd(dest_dir))
    return True


# ----------------------------------------------------------------------------- GUI
def run_gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    root = tk.Tk()
    root.title(APP_NAME + " - Instalador")
    root.geometry("560x330")
    root.resizable(False, False)
    try:  # icone FC na janela (Windows/Linux; no macOS o icone vem do .app)
        base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
        for cand in (os.path.join(base, "icon.png"), os.path.join(base, "icon", "icon.png")):
            if os.path.exists(cand):
                root.iconphoto(True, tk.PhotoImage(file=cand))
                break
    except Exception:
        pass

    state = {"scan": None}
    ttk.Label(root, text=APP_NAME, font=("Helvetica", 20, "bold")).pack(pady=(18, 2))
    ttk.Label(root, text="Plugin OpenFX para DaVinci Resolve").pack()
    status = tk.StringVar()
    dest_var = tk.StringVar()
    ttk.Label(root, textvariable=status, wraplength=500, justify="center").pack(pady=(18, 4))
    ttk.Label(root, textvariable=dest_var, wraplength=500, justify="center", foreground="#666").pack()
    btns = ttk.Frame(root)
    btns.pack(side="bottom", pady=22)

    def refresh():
        os_err = check_os()
        if os_err:
            status.set(os_err); install_btn.state(["disabled"]); return
        res = detect_resolve()
        if res or state["scan"] and state["scan"]["resolve"]:
            found = res or state["scan"]["resolve"][0]
            status.set("DaVinci Resolve encontrado:\n" + found)
        else:
            status.set("DaVinci Resolve nao foi encontrado automaticamente.\nClique em \"Localizar o Resolve...\" e escolha a pasta onde ele esta instalado.")
        dest_var.set("Destino do plugin: " + choose_destination(state["scan"]))

    def do_locate():
        d = filedialog.askdirectory(title="Escolha a pasta onde o DaVinci Resolve esta instalado")
        if not d:
            return
        root.config(cursor="watch"); root.update_idletasks()
        state["scan"] = scan_for_resolve(d)
        root.config(cursor="")
        if not (state["scan"]["resolve"] or state["scan"]["ofx_dirs"]):
            messagebox.showwarning(APP_NAME, "Nada do DaVinci Resolve foi encontrado nessa pasta.\nEscolha outra pasta (por exemplo a que contem o Resolve) ou instale mesmo assim na pasta padrao OFX.")
        refresh()

    def do_install():
        dest = choose_destination(state["scan"])
        root.config(cursor="watch"); root.update_idletasks()
        try:
            final = install(dest)
        except (InstallError, subprocess.CalledProcessError) as e:
            root.config(cursor="")
            messagebox.showerror(APP_NAME, str(e)); return
        root.config(cursor="")
        messagebox.showinfo(APP_NAME, "Plugin instalado em:\n%s\n\nFeche e reabra o DaVinci Resolve. Na pagina Color, procure \"%s\" em OpenFX." % (final, APP_NAME))
        root.destroy()

    def do_uninstall():
        dest = choose_destination(state["scan"])
        try:
            ok = uninstall(dest)
        except InstallError as e:
            messagebox.showerror(APP_NAME, str(e)); return
        messagebox.showinfo(APP_NAME, "Plugin removido." if ok else "O plugin nao estava instalado em " + dest)

    install_btn = ttk.Button(btns, text="Instalar", command=do_install)
    install_btn.grid(row=0, column=0, padx=6)
    ttk.Button(btns, text="Localizar o Resolve...", command=do_locate).grid(row=0, column=1, padx=6)
    ttk.Button(btns, text="Desinstalar", command=do_uninstall).grid(row=0, column=2, padx=6)
    refresh()
    root.mainloop()


def main(argv):
    if "--find" in argv:
        res = scan_for_resolve(argv[argv.index("--find") + 1])
        print(res)
        return 0
    if "--cli" in argv:
        dest = argv[argv.index("--dest") + 1] if "--dest" in argv else choose_destination(None)
        try:
            if "--uninstall" in argv:
                print("removed" if uninstall(dest) else "not installed")
            else:
                print("installed at", install(dest, dry_run="--dry-run" in argv))
        except InstallError as e:
            print("ERRO:", e)
            return 1
        return 0
    run_gui()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
