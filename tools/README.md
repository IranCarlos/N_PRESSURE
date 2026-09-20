# tools/ — Utilitários de desenvolvimento

- `sim/` — simulador de planta pneumática no host (Fase 3): modelo de 1ª/2ª ordem do manguito,
  atraso de motor, vazão das válvulas, vazamento e ruído do sensor. O mesmo código C do
  controlador (`Core/Services/Control_Loop`, quando desacoplado da HAL) roda contra esse
  simulador em `make test`.
- `make_bootanim.py` — converte um GIF/vídeo em `.anim` (frames RGB565 brutos) para a animação de
  boot lida do SD (Fase 6, Apêndice B do prompt de projeto).
- `gen_dev_key.py` — gera a chave de desenvolvedor (HMAC-SHA256, 32 bytes) usada na autenticação
  DEV do protocolo USB (Fase 7, Apêndice A). A chave em si nunca vai para o repositório.
- `fake_device/` — dispositivo virtual que fala o protocolo do Apêndice A, para desenvolver os
  apps de PC (`pc/`) sem hardware.

Ainda não implementado — cada um nasce na fase do prompt de projeto indicada acima. Ver
`Docs/dev/00-auditoria.md` para o estado atual.
