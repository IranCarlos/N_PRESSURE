# Empacota Python, Qt e bibliotecas numa pasta so: o PC de destino nao precisa de nada.
# A chave DEV nunca entra aqui: quem faz manutencao a recebe por fora.
import os

ROOT = os.path.abspath(os.path.join(SPECPATH, ".."))
BUILD = os.environ["NP_BUILD_DIR"]

a = Analysis(
    [os.path.join(ROOT, "app", "__main__.py")],
    pathex=[ROOT],
    hiddenimports=["serial.tools.list_ports_windows"],
    excludes=["pytest", "tkinter", "PyQt5", "PyQt6", "matplotlib", "scipy", "pandas", "IPython"],
)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name="nPressure", console=False,
          icon=os.path.join(BUILD, "nPressure.ico"), upx=False)
coll = COLLECT(exe, a.binaries, a.datas, name="nPressure", upx=False)
