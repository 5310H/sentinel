import os
import re

directories = [
    "components/engine",
    "components/comms",
    "components/mqtt_app",
    "components/esphome_api",
    "components/esphome-c-api",
    "components/camera",
    "components/tuya",
    "main",
    "test_linux"
]

def process_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    original = content

    # Remove manual externs
    content = re.sub(r'extern\s+config_t\s+config\s*;\s*(//.*)?\n?', '', content)
    content = re.sub(r'extern\s+network_t\s+net_cfg\s*;\s*(//.*)?\n?', '', content)
    content = re.sub(r'extern\s+zone_t\s+zones\[[^\]]*\]\s*;\s*(//.*)?\n?', '', content)
    content = re.sub(r'extern\s+int\s+z_count\s*;\s*(//.*)?\n?', '', content)
    content = re.sub(r'extern\s+relay_t\s+relays\[[^\]]*\]\s*;\s*(//.*)?\n?', '', content)
    content = re.sub(r'extern\s+int\s+r_count\s*;\s*(//.*)?\n?', '', content)
    content = re.sub(r'extern\s+user_t\s+users\[[^\]]*\]\s*;\s*(//.*)?\n?', '', content)
    content = re.sub(r'extern\s+int\s+u_count\s*;\s*(//.*)?\n?', '', content)
    content = re.sub(r'extern\s+esphome_device_t\s+esphome_devices\[[^\]]*\]\s*;\s*(//.*)?\n?', '', content)
    content = re.sub(r'extern\s+int\s+esphome_count\s*;\s*(//.*)?\n?', '', content)

    # Note: `cJSON *config` or `esp_http_client_config_t config` exists. We must carefully only replace `config.` and `config->` if it's the global object.
    # We will replace `config.` with `storage_get_config()->` except where `esp_http_client_config_t config` or `cJSON *config` or `mbedtls_ssl_config` is declared in the same function.
    # To be safer, we can just replace config.pin, config.entry_delay, config.exit_delay, etc.
    config_fields = [
        'account_id', 'pin', 'name', 'address1', 'address2', 'city', 'state', 'zip_code', 
        'email', 'phone', 'instructions', 'latitude', 'longitude', 'accuracy', 
        'monitor_service_id', 'monitor_service_key', 'monitoring_url', 'notify', 
        'nl_ids', 'is_monitor_fire', 'is_monitor_police', 'is_monitor_medical', 'is_monitor_other',
        'smtp_server', 'smtp_port', 'smtp_user', 'smtp_pass',
        'mqtt_server', 'mqtt_port', 'mqtt_user', 'mqtt_pass',
        'telegram_id', 'telegram_token', 'is_telegram_enabled',
        'nvrserver_url', 'haintegration_url',
        'entry_delay', 'exit_delay', 'cancel_delay'
    ]
    for field in config_fields:
        content = re.sub(r'\bconfig\.' + field + r'\b', r'storage_get_config()->' + field, content)
        content = re.sub(r'\(&config\)->' + field + r'\b', r'storage_get_config()->' + field, content)
        content = re.sub(r'\bconfig->' + field + r'\b', r'storage_get_config()->' + field, content)

    # Some functions passed `&config` as an argument (e.g. `smtp_alert_all_contacts(&config...)`)
    # We can replace `&config` with `storage_get_config()` if we know it's meant to be the global.
    content = re.sub(r'\(&config\)', 'storage_get_config()', content)
    content = re.sub(r'&\s*config\b', 'storage_get_config()', content)

    # Replace usages of net_cfg
    content = re.sub(r'\bnet_cfg\.', 'storage_get_network()->', content)

    # Replace usages of z_count, r_count, etc.
    content = re.sub(r'\bz_count\b', 'storage_get_zone_count()', content)
    content = re.sub(r'\br_count\b', 'storage_get_relay_count()', content)
    content = re.sub(r'\bu_count\b', 'storage_get_user_count()', content)
    content = re.sub(r'\besphome_count\b', 'storage_get_esphome_count()', content)

    # Replace usages of zones[i], relays[i], etc.
    content = re.sub(r'&?zones\[([^\]]+)\]\.', r'storage_get_zone(\1)->', content)
    content = re.sub(r'&?zones\[([^\]]+)\]', r'storage_get_zone(\1)', content)
    
    content = re.sub(r'&?relays\[([^\]]+)\]\.', r'storage_get_relay(\1)->', content)
    content = re.sub(r'&?relays\[([^\]]+)\]', r'storage_get_relay(\1)', content)

    content = re.sub(r'&?users\[([^\]]+)\]\.', r'storage_get_user(\1)->', content)
    content = re.sub(r'&?users\[([^\]]+)\]', r'storage_get_user(\1)', content)

    content = re.sub(r'&?esphome_devices\[([^\]]+)\]\.', r'storage_get_esphome_device(\1)->', content)
    content = re.sub(r'&?esphome_devices\[([^\]]+)\]', r'storage_get_esphome_device(\1)', content)

    if content != original:
        with open(filepath, 'w') as f:
            f.write(content)
        print(f"Updated {filepath}")

for d in directories:
    for root, dirs, files in os.walk(d):
        for f in files:
            if f.endswith('.c') or f.endswith('.h'):
                filepath = os.path.join(root, f)
                process_file(filepath)
