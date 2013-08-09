#ifndef CAN_EMULATOR

#include "interface/usb.h"
#include "can/canread.h"
#include "interface/uart.h"
#include "interface/network.h"
#include "signals.h"
#include "util/log.h"
#include "cJSON.h"
#include "pipeline.h"
#include "util/timer.h"
#include "lights.h"
#include "power.h"
#include "bluetooth.h"
#include "platform/platform.h"
#include <stdint.h>
#include <stdlib.h>

#define BUS_STATS_LOG_FREQUENCY_S 5
#define CAN_MESSAGE_TOTAL_BIT_SIZE (64 + 11)

namespace uart = openxc::interface::uart;
namespace network = openxc::interface::network;
namespace usb = openxc::interface::usb;
namespace lights = openxc::lights;
namespace can = openxc::can;
namespace platform = openxc::platform;
namespace time = openxc::util::time;
namespace signals = openxc::signals;

using openxc::can::lookupCommand;
using openxc::can::lookupSignal;
using openxc::signals::initialize;
using openxc::signals::getCanBuses;
using openxc::signals::getCanBusCount;
using openxc::signals::getCommands;
using openxc::signals::getCommandCount;
using openxc::signals::getSignals;
using openxc::signals::getSignalCount;
using openxc::signals::decodeCanMessage;

extern Pipeline pipeline;

/* Forward declarations */

void receiveCan(Pipeline*, CanBus*);
void initializeAllCan();
bool receiveWriteRequest(uint8_t*);
void updateDataLights();
void logBusStatistics();

static uint8_t MESSAGE_IDS[0x7ff];

void setup() {
    initializeAllCan();
    signals::initialize();
}

void loop() {
    for(int i = 0; i < getCanBusCount(); i++) {
        receiveCan(&pipeline, &getCanBuses()[i]);
    }

    usb::read(pipeline.usb, receiveWriteRequest);
    uart::read(pipeline.uart, receiveWriteRequest);
    network::read(pipeline.network, receiveWriteRequest);

    for(int i = 0; i < getCanBusCount(); i++) {
        can::write::processWriteQueue(&getCanBuses()[i]);
    }

    updateDataLights();
    openxc::signals::loop();
    logBusStatistics();
}

void logBusStatistics() {
    static unsigned long lastTimeLogged;
    if(time::systemTimeMs() - lastTimeLogged > BUS_STATS_LOG_FREQUENCY_S * 1000) {
        int messageCount = 0;
        for(int i = 0; i < 0x7ff; i++) {
            if(MESSAGE_IDS[i] == 1) {
                ++messageCount;
            }
        }
        debug("Unique CAN message IDs received across all buses: %d",
                messageCount);
        for(int i = 0; i < getCanBusCount(); i++) {
            CanBus* bus = &getCanBuses()[i];
            float totalDataKB = bus->messagesReceived *
                    CAN_MESSAGE_TOTAL_BIT_SIZE / 8192;
            debug("CAN messages received since startup on bus %d: %d",
                    bus->address, bus->messagesReceived);
            debug("Data received on bus %d since startup: %f KB", bus->address,
                    totalDataKB);
            debug("Aggregate throughput on bus %d since startup: %f KB / s",
                    bus->address, totalDataKB / (time::uptimeMs() / 1000));
        }

        lastTimeLogged = time::systemTimeMs();
    }
}

/* Public: Update the color and status of a board's light that shows the status
 * of the CAN bus. This function is intended to be called each time through the
 * main program loop.
 */
void updateDataLights() {
    static bool busWasActive;
    bool busActive = false;
    for(int i = 0; i < getCanBusCount(); i++) {
        busActive = busActive || can::busActive(&getCanBuses()[i]);
    }

    if(!busWasActive && busActive) {
        debug("CAN woke up - enabling LED");
        lights::enable(lights::LIGHT_A, lights::COLORS.blue);
        busWasActive = true;
    } else if(!busActive && (busWasActive || time::uptimeMs() >
            (unsigned long)openxc::can::CAN_ACTIVE_TIMEOUT_S * 1000)) {
        // stay awake at least CAN_ACTIVE_TIMEOUT_S after power on
#ifndef TRANSMITTER
#ifndef __DEBUG__
        busWasActive = false;
        platform::suspend(&pipeline);
#endif
#endif
    }
}

void initializeAllCan() {
    for(int i = 0; i < getCanBusCount(); i++) {
        can::initialize(&(getCanBuses()[i]));
    }
}

void receiveRawWriteRequest(cJSON* idObject, cJSON* root) {
    uint32_t id = idObject->valueint;
    cJSON* dataObject = cJSON_GetObjectItem(root, "data");
    if(dataObject == NULL) {
        debug("Raw write request missing data", id);
        return;
    }

    char* dataString = dataObject->valuestring;
    char* end;
    // TODO hard coding bus 0 right now, but it should support sending on either
    CanMessage message = {&getCanBuses()[0], id};
    can::write::enqueueMessage(&message, strtoull(dataString, &end, 16));
}

void receiveTranslatedWriteRequest(cJSON* nameObject, cJSON* root) {
    char* name = nameObject->valuestring;
    cJSON* value = cJSON_GetObjectItem(root, "value");

    // Optional, may be NULL
    cJSON* event = cJSON_GetObjectItem(root, "event");

    CanSignal* signal = lookupSignal(name, getSignals(), getSignalCount(),
            true);
    if(signal != NULL) {
        if(value == NULL) {
            debug("Write request for %s missing value", name);
            return;
        }
        can::write::sendSignal(signal, value, getSignals(), getSignalCount());
    } else {
        CanCommand* command = lookupCommand(name, getCommands(),
                getCommandCount());
        if(command != NULL) {
            command->handler(name, value, event, getSignals(),
                    getSignalCount());
        } else {
            debug("Writing not allowed for signal with name %s", name);
        }
    }
}

bool receiveWriteRequest(uint8_t* message) {
    cJSON *root = cJSON_Parse((char*)message);
    bool foundMessage = false;
    if(root != NULL) {
        foundMessage = true;
        cJSON* nameObject = cJSON_GetObjectItem(root, "name");
        if(nameObject == NULL) {
            cJSON* idObject = cJSON_GetObjectItem(root, "id");
            if(idObject == NULL) {
                debug("Write request is malformed, "
                        "missing name or id: %s", message);
            } else {
                receiveRawWriteRequest(idObject, root);
            }
        } else {
            receiveTranslatedWriteRequest(nameObject, root);
        }
        cJSON_Delete(root);
    } else {
        debug("No valid JSON in incoming buffer yet -- "
                "if it's valid, may be out of memory");
    }
    return foundMessage;
}

/*
 * Check to see if a packet has been received. If so, read the packet and print
 * the packet payload to the uart monitor.
 */
void receiveCan(Pipeline* pipeline, CanBus* bus) {
    // TODO what happens if we process until the queue is empty?
    if(!QUEUE_EMPTY(CanMessage, &bus->receiveQueue)) {

        CanMessage message = QUEUE_POP(CanMessage, &bus->receiveQueue);
        decodeCanMessage(pipeline, bus, message.id, message.data);
        bus->lastMessageReceived = time::systemTimeMs();

        MESSAGE_IDS[message.id] = 1;
        ++bus->messagesReceived;
    }
}

void reset() {
    initializeAllCan();
}

#endif // CAN_EMULATOR
