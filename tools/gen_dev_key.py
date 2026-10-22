#!/usr/bin/env python3
"""
Gera uma chave DEV de 32 bytes (HMAC-SHA256) para a autenticacao USB da
Fase 7.2 (Core/Services/UsbProto/Inc/usb_proto_auth.h). So' imprime - nunca
escreve em arquivo nenhum do repositorio ("a chave em si nunca vai pro
repositorio", ver tools/README.md).

Uso:
    python tools/gen_dev_key.py

Copie a saida (o array C) para o SEU `Core/Services/UsbProto/Inc/
dev_key_secret.h` local (copiado de dev_key_secret.h.example, que esta' no
.gitignore) e habilite `BRINGUP_PROVISION_DEV_KEY` em Core/Src/main.c por
UMA bancada, pra gravar a chave na EEPROM - depois volte a flag pra 0.
"""
import secrets


def main():
    key = secrets.token_bytes(32)
    c_array = ", ".join(f"0x{b:02x}" for b in key)
    print("Chave DEV gerada (32 bytes aleatorios, nunca comite isto em arquivo versionado):\n")
    print(f"hex: {key.hex()}\n")
    print("Array C (cole em dev_key_secret.h, campo DEV_KEY_PROVISION):")
    print(f"    {{ {c_array} }}")


if __name__ == "__main__":
    main()
