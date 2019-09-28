idf_component_register(SRCS "main.c"
		"config.c"
		"log.c"
		"nmea.c"
		"ntrip_caster.c"
		"ntrip_client.c"
		"ntrip_server.c"
		"socket_client.c"
		"socket_server.c"
		"status_led.c"
		"uart.c"
		"util.c"
		"web_server.c"
		"wifi.c"
        INCLUDE_DIRS "include")

spiffs_create_partition_image(www ../www FLASH_IN_PROJECT)
