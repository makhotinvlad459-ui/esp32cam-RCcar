#include "ble_manager.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "BLE_MGR";

// Статические переменные
static bool is_connected = false;
static uint16_t gatt_svr_chr_val_handle;
static uint16_t conn_handle = 0;
static ble_command_callback_t user_command_callback = NULL;

// UUIDs
static const ble_uuid16_t gatt_svr_svc_uuid = {
    .u = { .type = BLE_UUID_TYPE_16 },
    .value = BLE_SERVICE_UUID
};

static const ble_uuid16_t gatt_svr_chr_uuid = {
    .u = { .type = BLE_UUID_TYPE_16 },
    .value = BLE_CHARACTERISTIC_UUID
};

// Прототипы статических функций
static void ble_host_task(void *param);
static void ble_app_on_sync(void);
static void ble_app_on_reset(int reason);
static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
static int gatt_svr_init(void);
static void restart_advertising(void);

// Реализация функций

static void restart_advertising(void) {
    ESP_LOGI(TAG, "🔄 Starting advertising...");
    
    struct ble_gap_adv_params adv_params = {0};
    adv_params.itvl_min = 800;  
    adv_params.itvl_max = 1600;
    struct ble_hs_adv_fields fields = {0};
    
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)BLE_DEVICE_NAME;
    fields.name_len = strlen(BLE_DEVICE_NAME);
    fields.name_is_complete = 1;
    
    static ble_uuid16_t uuid16_list[] = {{ .u = { .type = BLE_UUID_TYPE_16 }, .value = BLE_SERVICE_UUID }};
    fields.uuids16 = uuid16_list;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields: %d", rc);
    }
    
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                         &adv_params, NULL, NULL);
    
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
    } else {
        ESP_LOGI(TAG, "✅ Advertising started");
    }
}

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            const char *status = is_connected ? "CONNECTED" : "READY";
            struct os_mbuf *om = ble_hs_mbuf_from_flat(status, strlen(status));
            ctxt->om = om;
            break;
        }
            
        case BLE_GATT_ACCESS_OP_WRITE_CHR: {
            uint16_t data_len = OS_MBUF_PKTLEN(ctxt->om);
            
            if (data_len > 0 && user_command_callback != NULL) {
                char command[64] = {0};
                int rc = ble_hs_mbuf_to_flat(ctxt->om, command, sizeof(command) - 1, NULL);
                
                if (rc == 0) {
                    user_command_callback(command, data_len);
                }
            }
            break;
        }
            
        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
    return 0;
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_svr_chr_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &gatt_svr_chr_val_handle,
            },
            {0}
        }
    },
    {0}
};

static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];
    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_SVC:
            ESP_LOGI(TAG, "Service registered: %s", ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf));
            break;
        case BLE_GATT_REGISTER_OP_CHR:
            ESP_LOGI(TAG, "Characteristic registered: %s", ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf));
            break;
        default:
            break;
    }
}

static int gatt_svr_init(void) {
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) return rc;
    return ble_gatts_add_svcs(gatt_svr_svcs);
}

static void ble_app_on_sync(void) {
    ble_hs_util_ensure_addr(0);
    ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    restart_advertising();
}

static void ble_app_on_reset(int reason) {
    ESP_LOGE(TAG, "BLE reset: %d", reason);
}

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "BLE Connected");
            is_connected = true;
            conn_handle = event->connect.conn_handle;
            break;
            
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE Disconnected");
            is_connected = false;
            conn_handle = 0;
            restart_advertising();
            break;
            
        default:
            break;
    }
    return 0;
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_manager_init(ble_command_callback_t cmd_callback) {
    ESP_LOGI(TAG, "Initializing NimBLE BLE stack");
    user_command_callback = cmd_callback;
    nimble_port_init();
    
    ble_hs_cfg.reset_cb = ble_app_on_reset;
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    
    ble_svc_gap_init();
    ble_svc_gatt_init();
    
    int rc = gatt_svr_init();
    if (rc != 0) return ESP_FAIL;
    
    // Регистрация слушателя событий GAP
    static struct ble_gap_event_listener gap_event_listener;
    rc = ble_gap_event_listener_register(&gap_event_listener, ble_gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to register GAP event listener: %d", rc);
    }
    
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE initialized successfully");
    return ESP_OK;
}

bool ble_manager_is_connected(void) {
    return is_connected;
}

void ble_manager_print_info(void) {
    ESP_LOGI(TAG, "=== BLE INFO ===");
    ESP_LOGI(TAG, "Connected: %s", is_connected ? "yes" : "no");
    ESP_LOGI(TAG, "Device name: %s", BLE_DEVICE_NAME);
    ESP_LOGI(TAG, "================");
}

esp_err_t ble_manager_send_response(const char *response) {
    return ESP_OK;
}

// ==================== НОВАЯ ФУНКЦИЯ ====================
void ble_manager_send_command(const char *command) {
    if (!is_connected) {
        ESP_LOGW(TAG, "BLE не подключен, команда не отправлена: %s", command);
        return;
    }
    
    ESP_LOGI(TAG, "📤 Отправка BLE команды: %s", command);
    
    // Отправляем через характеристику 0xffe1 (write)
    struct os_mbuf *om = ble_hs_mbuf_from_flat(command, strlen(command));
    if (om == NULL) {
        ESP_LOGE(TAG, "Ошибка создания буфера для команды");
        return;
    }
    
    int rc = ble_gatts_notify_custom(conn_handle, gatt_svr_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Ошибка отправки команды: %d", rc);
    } else {
        ESP_LOGI(TAG, "✅ Команда отправлена: %s", command);
    }
}