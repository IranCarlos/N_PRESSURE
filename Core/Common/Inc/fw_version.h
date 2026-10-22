/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-12
 */

#ifndef FW_VERSION_H
#define FW_VERSION_H

/**
 * @file fw_version.h
 * @brief Identidade do dispositivo/firmware — fonte unica usada pela tela
 *        de Informacoes da IHM e por StorageFormat_RecInfo_t.device_id/
 *        firmware_version (Core/Services/Audit_Trail/Inc/storage_format.h)
 *        ao montar o registro de um ensaio real. Antes desses dois pontos
 *        existirem, esses campos so' eram preenchidos por teste de host
 *        (test/host/test_storage_format.c) com valores fixos ("nPressure
 *        NP001" — nome errado, corrigido aqui) — nao havia nenhuma fonte
 *        real no firmware.
 *
 * DEVICE_ID usa o nome comercial oficial, "myPRESSURE" (marca da Nobrega
 * Technology — ver Docs/Nobrega Technology_e_myPRESSURE_Oficial.pdf), nao
 * o nome do diretorio do projeto ("Angiologista"/"N_PRESSURE").
 *
 * Header-folha (Core/Common), sem dependencias — qualquer camada pode
 * incluir sem inverter a direcao App -> Services -> Bsp -> HAL.
 */

#define FIRMWARE_VERSION "0.6.0"
#define DEVICE_ID        "myPRESSURE NP001"

#endif /* FW_VERSION_H */
