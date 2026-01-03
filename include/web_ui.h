#pragma once
#include <Arduino.h>
#include <WebServer.h>

void webui_begin(WebServer& server, const IPAddress& sta_dns);
