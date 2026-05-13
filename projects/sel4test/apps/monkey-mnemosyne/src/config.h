/*
 * Monkey Lab for JK.
 *
 * Created on 2025.2.14 at Yushan, Shangrao
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */


#pragma once

#include <adl/sys/types.h>

enum class LabApp {
    App1,
    App2
};

struct {
    const char* mnemosyneIp = "";
    adl::uint16_t mnemosynePort = 10100;

    const char* app1Key = "f578bd06-6f8e-42b3-8be9-860c7c645549";
    const char* app2Key = "8370c1fe-d422-42d9-a261-05aed72313c3";
    LabApp labApp = LabApp::App1;
} static const labConfig;
