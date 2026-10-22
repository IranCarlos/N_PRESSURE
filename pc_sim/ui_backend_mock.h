/**
 * @file ui_backend_mock.h
 * @brief Controles do mock de ui_backend.h (teclas do simulador).
 */
#ifndef UI_BACKEND_MOCK_H
#define UI_BACKEND_MOCK_H

#include <stdint.h>

void UiMock_Init(void);
void UiMock_Step(void);                    /* chamar a cada volta do loop */
void UiMock_UserButton(void);              /* pedal / botao do usuario */
void UiMock_InjectError(uint16_t code);    /* 1, 2, 3, 4, 13 */
void UiMock_ClearError(void);
void UiMock_ToggleSd(void);
void UiMock_ToggleRtc(void);

#endif /* UI_BACKEND_MOCK_H */
