#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "driver/ledc.h"
#include "driver/adc.h"
#include "esp_system.h"
#include "esp_task.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "blynk.h"

#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASS CONFIG_WIFI_PASSWORD
#define BLYNK_TOKEN CONFIG_BLYNK_TOKEN
#define BLYNK_SERVER CONFIG_BLYNK_SERVER

static const char *tag = "blynk-example";

#define BLY_LED 22

static void led_init(void)
{
    gpio_pad_select_gpio(BLY_LED);
    gpio_set_direction(BLY_LED, GPIO_MODE_INPUT_OUTPUT);
	gpio_set_level(BLY_LED, 0);
}

#define GPIO_REC_IRQ		36
#define GPIO_IRQ_PIN_SEL	(1ULL<<GPIO_REC_IRQ)

#define ESP_INTR_FLAG_DEFAULT 0

static xQueueHandle gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
	uint32_t gpio_num = (uint32_t) arg;
	xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_task_example(void* arg)
{
	uint32_t io_num;
	uint8_t toggle = 0;

	for(;;) {
		if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
			if((io_num == GPIO_REC_IRQ) && (gpio_get_level(io_num) == 0))
			{
				toggle = gpio_get_level(BLY_LED);
				toggle ^= (1 << 0);
				gpio_set_level(BLY_LED, toggle);
				vTaskDelay(100 / portTICK_PERIOD_MS);
			}
		}
	}
}

static void irq_intr_init(void)
{
	gpio_config_t io_conf;
	//interrupt of rising edge
	io_conf.intr_type = GPIO_PIN_INTR_NEGEDGE;
	//bit mask of the pins, use GPIO4/5 here
	io_conf.pin_bit_mask = GPIO_IRQ_PIN_SEL;
	//set as input mode    
	io_conf.mode = GPIO_MODE_INPUT;
	//enable pull-up mode
	io_conf.pull_up_en = 1;
	gpio_config(&io_conf);

	//create a queue to handle gpio event from isr
	gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
	//start gpio task
	xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, (CONFIG_ESP32_PTHREAD_TASK_PRIO_DEFAULT - 1), NULL);

	//install gpio isr service
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	//hook isr handler for specific gpio pin
	gpio_isr_handler_add(GPIO_REC_IRQ, gpio_isr_handler, (void*) GPIO_REC_IRQ);
}

static esp_err_t event_handler(void *arg, system_event_t *event) {
	switch (event->event_id) {
		case SYSTEM_EVENT_STA_START:
		{
			ESP_LOGI(tag, "WiFi started");
			/* Auto connect feature likely not implemented in WiFi lib, so do it manually */
			bool ac;
			ESP_ERROR_CHECK(esp_wifi_get_auto_connect(&ac));
			if (ac) {
				esp_wifi_connect();
			}
			break;
		}

		case SYSTEM_EVENT_STA_CONNECTED:
			/* enable ipv6 */
			tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);
			break;

		case SYSTEM_EVENT_STA_GOT_IP:
			break;

		case SYSTEM_EVENT_STA_DISCONNECTED:
			/* This is a workaround as ESP32 WiFi libs don't currently
			   auto-reassociate. */
			esp_wifi_connect();
			break;

		default:
			break;
	}
	return ESP_OK;
}

static void wifi_conn_init() {
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	// Get config from NVS
	wifi_config_t wifi_config;
	ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config));

	if (!wifi_config.sta.ssid[0]) {
		memset(&wifi_config, 0, sizeof(wifi_config));

		strlcpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
		strlcpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

		if (!wifi_config.sta.ssid[0]) {
			return;
		}

		ESP_LOGI(tag, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
		ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	}

	ESP_ERROR_CHECK(esp_wifi_start());
}

/* Blynk client state handler */
static void state_handler(blynk_client_t *c, const blynk_state_evt_t *ev, void *data) {
	ESP_LOGI(tag, "state: %d\n", ev->state);
}

static void cmd_printf(const char *func, const char *cmd, int argc, char **argv)
{
	printf("\t\t%s %s ", func, cmd);

	for(int i = 0; i < argc; i++)
	{
		printf("%s ", argv[i]);
	}
	printf("\n");
}

/* Virtual write handler */
static void vw_handler(blynk_client_t *c, uint16_t id, const char *cmd, int argc, char **argv, void *data) {

	cmd_printf("vw_handler", cmd, argc, argv);

	if (argc > 1 && (atoi(argv[0]) == BLY_LED))
	{
		gpio_set_level(BLY_LED, atoi(argv[1]));
	}
}

uint8_t prev_value = 0;

/* Virtual read handler */
static void vr_handler(blynk_client_t *c, uint16_t id, const char *cmd, int argc, char **argv, void *data) {
	if (!argc) {
		return;
	}

	cmd_printf("vr_handler", cmd, argc, argv);

	int pin = atoi(argv[0]);

	if(pin == BLY_LED)
	{
		uint8_t value = gpio_get_level(BLY_LED);
		
		if(prev_value != value)
		{
			/* Respond with `virtual write' command */
			blynk_send(c, BLYNK_CMD_HARDWARE, 0, "sii", "vw", BLY_LED, value);
		}
		prev_value = value;
	}
}

void app_main() {
	blynk_err_t ret;

	led_init();
	irq_intr_init();

	ESP_ERROR_CHECK(nvs_flash_init());
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

	/* Init WiFi */
	wifi_conn_init();

	blynk_client_t *client = malloc(sizeof(blynk_client_t));
	blynk_init(client);

	blynk_options_t opt = {
		.token = BLYNK_TOKEN,
		.server = BLYNK_SERVER,
		/* Use default timeouts */
	};

	blynk_set_options(client, &opt);

	/* Subscribe to state changes and errors */
	blynk_set_state_handler(client, state_handler, NULL);

	/* blynk_set_handler sets hardware (BLYNK_CMD_HARDWARE) command handler */
	blynk_set_handler(client, "vw", vw_handler, NULL);
	blynk_set_handler(client, "vr", vr_handler, NULL);

	/* Start Blynk client task */
	ret = blynk_start(client);
	ESP_LOGI(tag, "blynk_start ret[%d]", ret);
}
