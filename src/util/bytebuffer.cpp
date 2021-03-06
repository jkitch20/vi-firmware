#include "util/bytebuffer.h"
#include "strutil.h"
#include "util/log.h"

QUEUE_DEFINE(uint8_t)

void openxc::util::bytebuffer::processQueue(QUEUE_TYPE(uint8_t)* queue, bool (*callback)(uint8_t*)) {
    int length = QUEUE_LENGTH(uint8_t, queue);
    if(length == 0) {
        return;
    }

    uint8_t snapshot[length];
    QUEUE_SNAPSHOT(uint8_t, queue, snapshot);
    if(callback == NULL) {
        debug("Callback is NULL (%p) -- unable to handle queue at %p",
                callback, queue);
        return;
    }

    if(callback(snapshot)) {
        QUEUE_INIT(uint8_t, queue);
    } else if(QUEUE_FULL(uint8_t, queue)) {
        debug("Incoming write is too long");
        QUEUE_INIT(uint8_t, queue);
    } else if(strnchr((char*)snapshot, sizeof(snapshot) - 1, '\0') != NULL) {
        debug("Incoming buffered write corrupted (%s) -- clearing buffer",
                snapshot);
        QUEUE_INIT(uint8_t, queue);
    }
}

bool openxc::util::bytebuffer::messageFits(QUEUE_TYPE(uint8_t)* queue, uint8_t* message,
        int messageSize) {
    return queue != NULL && QUEUE_AVAILABLE(uint8_t, queue) >= messageSize + 2;
}

bool openxc::util::bytebuffer::conditionalEnqueue(QUEUE_TYPE(uint8_t)* queue, uint8_t* message,
        int messageSize) {
    if(messageFits(queue, message, messageSize)) {
        for(int i = 0; i < messageSize; i++) {
            QUEUE_PUSH(uint8_t, queue, (uint8_t)message[i]);
        }
        QUEUE_PUSH(uint8_t, queue, (uint8_t)'\r');
        QUEUE_PUSH(uint8_t, queue, (uint8_t)'\n');
        return true;
    }
    return false;
}
