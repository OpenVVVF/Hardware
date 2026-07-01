#include "Inverter/Control/CommandShell.h"
#include "Inverter/Command/CommandManager.h"
#include "Inverter/Command/CommandInitializer.h"
#include "Inverter/Command/CommandContext.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include "usart.h"

#include <cctype>
#include <cstring>

namespace Inverter {

static CommandShell s_instance;
static CommandContext s_commandContext;

CommandShell& commandShell() {
    return s_instance;
}

bool CommandShell::init() {
    if (m_initialized) {
        return true;
    }

    m_rx_head = 0;
    m_rx_tail = 0;
    m_line_len = 0;
    m_initialized = true;

    /* Clear any stale error/idle flags left from the power-up / debugger
     * transient before unmasking the UART interrupt. */
    __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_PEF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF | UART_CLEAR_OREF |
                                  UART_CLEAR_IDLEF);
    HAL_NVIC_ClearPendingIRQ(USART3_IRQn);

    HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart3, &m_rx_buf[0], 1U);
    if (status != HAL_OK) {
        Telemetry::printf("[SHELL] ERROR: HAL_UART_Receive_IT failed");
        return false;
    }

    /* Register all commands with the old-firmware command manager framework. */
    initializeCommands();
    CommandManager::instance().setContext(s_commandContext);

    Telemetry::printf("[SHELL] Command shell ready; type HELP for list");
    return true;
}

void CommandShell::onRxComplete() {
    /* HAL advances pRxBuffPtr after writing, so the received byte is always
     * at the start of the buffer we passed (m_rx_buf[0]). */
    uint8_t b = m_rx_buf[0];

    size_t next = (m_rx_head + 1U) % RX_BUF_SIZE;
    if (next != m_rx_tail) {
        m_rx_buf[m_rx_head] = b;
        m_rx_head = next;
    }

    /* Restart reception immediately. */
    HAL_UART_Receive_IT(&huart3, &m_rx_buf[0], 1U);
}

void CommandShell::recover() {
    if (!m_initialized) return;

    /* Clear error/idle flags and restart reception. */
    __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_PEF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF | UART_CLEAR_OREF |
                                  UART_CLEAR_IDLEF);
    HAL_NVIC_ClearPendingIRQ(USART3_IRQn);
    HAL_UART_Receive_IT(&huart3, &m_rx_buf[0], 1U);
}

void CommandShell::poll() {
    if (!m_initialized) {
        return;
    }

    while (true) {
        __disable_irq();
        bool empty = (m_rx_head == m_rx_tail);
        uint8_t b = empty ? 0U : m_rx_buf[m_rx_tail];
        if (!empty) {
            m_rx_tail = (m_rx_tail + 1U) % RX_BUF_SIZE;
        }
        __enable_irq();

        if (empty) {
            break;
        }

        /* Collect until newline or line buffer full. */
        if (b == '\r' || b == '\n') {
            if (m_line_len > 0) {
                m_line[m_line_len] = '\0';

                /* Make a local copy and reset the buffer before parsing. */
                char tmp[LINE_SIZE];
                std::strncpy(tmp, m_line, LINE_SIZE - 1);
                tmp[LINE_SIZE - 1] = '\0';

                m_line_len = 0;
                m_line[0] = '\0';

                /* Dispatch via the command manager framework. */
                CommandManager::instance().processLine(tmp);
            }
        } else if (m_line_len < LINE_SIZE - 1) {
            m_line[m_line_len++] = static_cast<char>(b);
        }
    }
}

} // namespace Inverter

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
    if (huart != nullptr && huart->Instance == USART3) {
        Inverter::commandShell().onRxComplete();
    }
}
