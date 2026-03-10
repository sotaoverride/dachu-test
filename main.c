#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"

// --- CONFIGURATION ---
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define VM_HOST "" 
#define VM_PORT 8080
#define VM_PATH "/report"
#define REED_PIN 14
#define QUEUE_SIZE 20
#define DEBOUNCE_TIME_MS 50  // Standard for mechanical reed switches

typedef struct { bool state; uint32_t time; } Event;
Event event_queue[QUEUE_SIZE];
volatile int q_read = 0;
volatile int q_write = 0;
bool is_net_busy = false;
bool current_state = false;
uint32_t last_debounce = 0;
// --- 1. THE INTERRUPT (ULTRA FAST) ---
void gpio_callback(uint gpio, uint32_t events) {
    bool s = gpio_get(REED_PIN);
    
    // Print instantly to UART so you know the hardware works
    printf("\n[!] INTERRUPT: %s", s ? "OPEN" : "CLOSED");
    
    int next = (q_write + 1) % QUEUE_SIZE;
    if (next != q_read && s != current_state && (to_ms_since_boot(get_absolute_time()) - last_debounce) > DEBOUNCE_TIME_MS)  {
        event_queue[q_write].state = s;
        event_queue[q_write].time = to_ms_since_boot(get_absolute_time());
        q_write = next;
	last_debounce = to_ms_since_boot(get_absolute_time();
	current_state = s;
    } else if (next == q_read) {
        printf(" (Queue Full!)");
    }
}

// 1. Update the error callback to be more descriptive
static void tcp_client_err(void *arg, err_t err) {
    // Note: arg might be NULL here if the connection failed early
    printf(" -> TCP Error: %d (Aborted/Reset)\n", err);
    
    // IMPORTANT: Do NOT call tcp_close() here. 
    // LwIP already deallocates the pcb on an error callback.
    is_net_busy = false;
}

// 2. Update the sent callback to ensure total closure
static err_t tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    printf(" -> Data Delivered\n");
    
    // Tell LwIP we don't want any more data and we're done
    tcp_arg(tpcb, NULL);
    tcp_sent(tpcb, NULL);
    tcp_err(tpcb, NULL);
    
    err_t close_err = tcp_close(tpcb);
    if (close_err != ERR_OK) {
        // If close fails (out of memory), we force abort
        tcp_abort(tpcb);
    }
    
    is_net_busy = false;
    return ERR_OK;
}
static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    if (err != ERR_OK) {
        tcp_abort(tpcb);
        is_net_busy = false;
        return ERR_OK;
    }

    Event *e = (Event*)arg;
    char body[64], req[256];
    snprintf(body, sizeof(body), "{\"status\":\"%s\",\"t\":%u}", e->state ? "open" : "closed", e->time);
    snprintf(req, sizeof(req), "POST /report HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s", 
             VM_HOST, (int)strlen(body), body);
    
    tcp_sent(tpcb, tcp_client_sent);
    tcp_err(tpcb, tcp_client_err);
    tcp_write(tpcb, req, strlen(req), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    return ERR_OK;
}

static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg) {
    if (ipaddr) {
        struct tcp_pcb *pcb = tcp_new();
        tcp_arg(pcb, arg);
        tcp_connect(pcb, ipaddr, VM_PORT, tcp_client_connected);
    } else {
        printf(" -> DNS Failed\n");
        is_net_busy = false;
    }
}

// --- 3. MAIN ---
int main() {
    stdio_init_all();
    sleep_ms(3000); 
    printf("\n--- ENTRY MONITOR START ---\n");

    gpio_init(REED_PIN);
    gpio_set_dir(REED_PIN, GPIO_IN);
    gpio_pull_up(REED_PIN);

    cyw43_arch_init();
    cyw43_arch_enable_sta_mode();
    cyw43_arch_wifi_connect_blocking(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

    // Register the interrupt
    gpio_set_irq_enabled_with_callback(REED_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    while(true) {
        cyw43_arch_poll();
        
        // Process the queue one by one
        if (q_read != q_write && !is_net_busy) {
            is_net_busy = true;
            Event *e = &event_queue[q_read];
            printf(">>> Processing: %s (T+%ums)", e->state ? "OPEN" : "CLOSED", e->time);
            
            ip_addr_t remote_addr;
            err_t dns_err = dns_gethostbyname(VM_HOST, &remote_addr, dns_callback, e);
            if (dns_err == ERR_OK) dns_callback(VM_HOST, &remote_addr, e);
            
            q_read = (q_read + 1) % QUEUE_SIZE;
        }
        
        sleep_ms(1);
    }
}
