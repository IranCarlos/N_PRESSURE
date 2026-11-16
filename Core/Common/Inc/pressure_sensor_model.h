/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-11-16
 *
 * Selecao do sensor de pressao montado na placa e TODOS os numeros que
 * dependem dele (curva, faixa valida, limites de setpoint/sobrepressao,
 * faixa da referencia de calibracao). Nenhum outro arquivo deve repetir
 * esses valores. O firmware escolhe o sensor pelo CMake
 * (cmake --preset Debug -DPRESSURE_SENSOR=MPXV5100GP, padrao MPXV7025DP),
 * que define PRESSURE_SENSOR_MODEL; sem ele (host, simulador) vale o padrao
 * abaixo.
 *
 * Folha sem dependencias (Core/Common): incluido pelo Pressure_Core (host e
 * alvo), Params, EEPROM, Control_Loop e Usb_Port.
 *
 * Ao trocar de sensor numa placa ja' em uso:
 *  - a calibracao gravada (offset/escala) era do sensor anterior: refazer a
 *    calibracao de 2 pontos com o manometro;
 *  - limites gravados na EEPROM (limite.setpoint_max etc.) continuam valendo
 *    se couberem na faixa nova: restaurar os padroes para pegar os novos;
 *  - bancada obrigatoria (controle, E001, tempos) na faixa nova.
 */
#ifndef PRESSURE_SENSOR_MODEL_H
#define PRESSURE_SENSOR_MODEL_H

#define PRESSURE_SENSOR_MPXV7025DP 1 /* diferencial -25..+25 kPa (187,5 mmHg) */
#define PRESSURE_SENSOR_MPXV5100GP 2 /* manometrico   0..100 kPa (750 mmHg)   */

#ifndef PRESSURE_SENSOR_MODEL
#define PRESSURE_SENSOR_MODEL PRESSURE_SENSOR_MPXV7025DP
#endif

#if PRESSURE_SENSOR_MODEL == PRESSURE_SENSOR_MPXV7025DP

#define PRESSURE_SENSOR_NAME "MPXV7025DP"
/* Curva (Vs = 5 V): 0.2 V = -25 kPa ... 4.7 V = +25 kPa (90 mV/kPa). */
#define PRESSURE_SENSOR_MIN_V   (0.2f)
#define PRESSURE_SENSOR_MAX_V   (4.7f)
#define PRESSURE_SENSOR_MIN_KPA (-25.0f)
#define PRESSURE_SENSOR_MAX_KPA (25.0f)
/* Abaixo disto a curva nao e' extrapolada (clamp). O zero atmosferico fica
 * no meio da curva (~2.5 V), entao o clamp em 0.2 V nunca afeta a medida. */
#define PRESSURE_SENSOR_CLAMP_MIN_V (0.2f)
/* Fora desta faixa (V no sensor) = cabo aberto/curto. */
#define PRESSURE_VALID_MIN_V (0.10f)
#define PRESSURE_VALID_MAX_V (4.90f)

/* Satura em 25 kPa = 187,5 mmHg: nada acima disso e' medido. Limite
 * absoluto (E001) e referencia de calibracao nao passam daqui. */
#define PRESSURE_P_MAX_ABS_LIMIT_MMHG (187.0f)
/* Teto da faixa de setpoint/alvo do teste de vazamento (decisao do usuario,
 * 2026-10-22: ate' 180 com alerta acima de 170). */
#define PRESSURE_SETPOINT_LIMIT_MMHG  (180.0f)
/* Padroes de fabrica. */
#define PRESSURE_P_MAX_ABS_DEFAULT_MMHG    (185.0f)
#define PRESSURE_SETPOINT_MAX_DEFAULT_MMHG (180.0f)
#define PRESSURE_SETPOINT_WARN_DEFAULT_MMHG (170.0f)

#elif PRESSURE_SENSOR_MODEL == PRESSURE_SENSOR_MPXV5100GP

#define PRESSURE_SENSOR_NAME "MPXV5100GP"
/* Curva (Vs = 5 V): Vout = Vs*(0.009*P + 0.04) -> 0.2 V = 0 kPa ...
 * 4.7 V = 100 kPa (45 mV/kPa, 6 mV/mmHg). 300 mmHg = 40 kPa = 2.0 V. */
#define PRESSURE_SENSOR_MIN_V   (0.2f)
#define PRESSURE_SENSOR_MAX_V   (4.7f)
#define PRESSURE_SENSOR_MIN_KPA (0.0f)
#define PRESSURE_SENSOR_MAX_KPA (100.0f)
/* Sensor manometrico: o zero atmosferico fica no PE' da curva e o offset
 * real varia de unidade para unidade (datasheet: 0.088..0.313 V). A curva
 * TEM que ser extrapolada abaixo de 0.2 V - com clamp em 0.2 V uma unidade
 * de offset 0.15 V leria "0" ate' ~8 mmHg e o rastreio do zero (que precisa
 * ver leituras abaixo do zero) quebraria. O offset nominal nao importa: o
 * zero atmosferico o remove. */
#define PRESSURE_SENSOR_CLAMP_MIN_V (0.05f)
/* 0.05 V: abaixo do menor offset de datasheet (0.088 V, ~0.084 V com
 * Vs = 4.75 V) com margem, e ainda detecta cabo aberto/curto a GND. */
#define PRESSURE_VALID_MIN_V (0.05f)
#define PRESSURE_VALID_MAX_V (4.90f)

/* O sensor le ate' 750 mmHg; o teto aqui e' do sistema (manguito, bomba,
 * EEPROM: setpoint limitado a 300). Limite absoluto acima do setpoint
 * maximo, como no projeto original (320/300). */
#define PRESSURE_P_MAX_ABS_LIMIT_MMHG (330.0f)
#define PRESSURE_SETPOINT_LIMIT_MMHG  (300.0f)
#define PRESSURE_P_MAX_ABS_DEFAULT_MMHG    (320.0f)
#define PRESSURE_SETPOINT_MAX_DEFAULT_MMHG (300.0f)
/* Sem alerta de setpoint (decisao do usuario, 2026-11-16): limiar = setpoint
 * maximo, entao nenhum setpoint permitido o dispara (o alerta e' setpoint >
 * limiar). Continua ajustavel pelo parametro limite.setpoint_alerta. */
#define PRESSURE_SETPOINT_WARN_DEFAULT_MMHG PRESSURE_SETPOINT_MAX_DEFAULT_MMHG

#else
#error "PRESSURE_SENSOR_MODEL invalido (ver pressure_sensor_model.h)"
#endif

/* Faixa plausivel da referencia (manometro) num ponto de calibracao: um
 * pouco abaixo de zero (ruido do manometro) ate' o limite absoluto. */
#define PRESSURE_CAL_REF_MIN_MMHG (-5.0f)
#define PRESSURE_CAL_REF_MAX_MMHG PRESSURE_P_MAX_ABS_LIMIT_MMHG

/* Coerencia entre os limites (setpoint max < limite absoluto <= teto etc.)
 * e' verificada para os DOIS modelos em test/host/test_params.c. */

#endif /* PRESSURE_SENSOR_MODEL_H */
