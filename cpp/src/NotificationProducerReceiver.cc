/******************************************************************************
 * Copyright (c) 2013-2014, AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

#include "NotificationProducerReceiver.h"
#include "Transport.h"
#include "NotificationConstants.h"
#include <alljoyn/notification/NotificationService.h>
#include <sstream>
#include <iostream>
#include "NotificationDismisserSender.h"
#include <alljoyn/services_common/Conversions.h>
#include <alljoyn/notification/LogModule.h>

using namespace ajn;
using namespace services;
using namespace qcc;
using namespace nsConsts;


NotificationProducerReceiver::NotificationProducerReceiver(ajn::BusAttachment* bus, QStatus& status) :
    NotificationProducer(bus, status), m_IsStopping(false)
{
    /**
     * Do not add code until the status that returned from the base class is verified.
     */
    if (status != ER_OK) {
        return;
    }

    status = AddMethodHandler(m_InterfaceDescription->GetMember(AJ_DISMISS_METHOD_NAME.c_str()),
                              static_cast<MessageReceiver::MethodHandler>(&NotificationProducerReceiver::Dismiss));
    if (status != ER_OK) {
        QCC_LogError(status, ("AddMethodHandler failed."));
        return;
    }

    pthread_mutex_init(&m_Lock, NULL);
    pthread_cond_init(&m_QueueChanged, NULL);
    pthread_create(&m_ReceiverThread, NULL, ReceiverThreadWrapper, this);
}

NotificationProducerReceiver::~NotificationProducerReceiver()
{
    QCC_DbgTrace(("start"));

    QCC_DbgTrace(("end"));
}

void NotificationProducerReceiver::unregisterHandler(BusAttachment* bus)
{
    pthread_mutex_lock(&m_Lock);
    while (!m_MessageQueue.empty()) {
        m_MessageQueue.pop();
    }
    m_IsStopping = true;
    pthread_cond_signal(&m_QueueChanged);
    pthread_mutex_unlock(&m_Lock);
    pthread_join(m_ReceiverThread, NULL);

    pthread_cond_destroy(&m_QueueChanged);
    pthread_mutex_destroy(&m_Lock);
}

void* NotificationProducerReceiver::ReceiverThreadWrapper(void* context)
{
    QCC_DbgTrace(("NotificationProducerReceiver::ReceiverThreadWrapper()"));
    NotificationProducerReceiver* pNotificationProducerReceiver = reinterpret_cast<NotificationProducerReceiver*>(context);
    if (pNotificationProducerReceiver == NULL) { // should not happen
        return NULL;
    }
    pNotificationProducerReceiver->Receiver();
    return NULL;
}

void NotificationProducerReceiver::Dismiss(const ajn::InterfaceDescription::Member* member, ajn::Message& msg)
{
    QCC_DbgTrace(("NotificationProducerReceiver::Dismiss()"));
    HandleMethodCall(member, msg);
}

void NotificationProducerReceiver::HandleMethodCall(const ajn::InterfaceDescription::Member* member, ajn::Message& msg)
{
    QCC_DbgTrace(("NotificationProducerReceiver::HandleMethodCall()"));
    const ajn::MsgArg* args = 0;
    size_t numArgs = 0;
    QStatus status = ER_OK;

    msg->GetArgs(numArgs, args);
    if (numArgs != 1) {
        status = ER_INVALID_DATA;
        goto exit;
    }
    int32_t msgId;
    status = args[0].Get(AJPARAM_INT.c_str(), &msgId);
    if (status != ER_OK) {
        goto exit;
    }

    QCC_DbgPrintf(("msgId:%d", msgId));

    MethodReply(msg, args, 0);
    pthread_mutex_lock(&m_Lock);
    {
        MsgQueueContent msgQueueContent(msgId);
        m_MessageQueue.push(msgQueueContent);
        QCC_DbgPrintf(("HandleMethodCall() - message pushed"));
    }
    pthread_cond_signal(&m_QueueChanged);
    pthread_mutex_unlock(&m_Lock);

exit:
    if (status != ER_OK) {
        MethodReply(msg, ER_INVALID_DATA);
        QCC_LogError(status, (""));
    }
}

void NotificationProducerReceiver::Receiver()
{
    pthread_mutex_lock(&m_Lock);
    while (!m_IsStopping) {
        while (!m_MessageQueue.empty()) {
            MsgQueueContent message = m_MessageQueue.front();
            m_MessageQueue.pop();
            QCC_DbgPrintf(("NotificationProducerReceiver::ReceiverThread() - got a message."));
            pthread_mutex_unlock(&m_Lock);
            Transport::getInstance()->deleteMsg(message.m_MsgId);
            sendDismissSignal(message.m_MsgId);
            pthread_mutex_lock(&m_Lock);
        }
        pthread_cond_wait(&m_QueueChanged, &m_Lock);
    }
    pthread_mutex_unlock(&m_Lock);
}

QStatus NotificationProducerReceiver::sendDismissSignal(int32_t msgId)
{
    QCC_DbgTrace(("Notification::sendDismissSignal() called"));
    QStatus status;
    MsgArg msgIdArg;

    msgIdArg.Set(nsConsts::AJPARAM_INT.c_str(), msgId);

    MsgArg dismisserArgs[nsConsts::AJ_DISMISSER_NUM_PARAMS];
    dismisserArgs[0] = msgIdArg;
    dismisserArgs[1] = m_AppIdArg;

    /**Code commented below is for future use
     * In case dismiss signal will not need to be sent via different object path each time.
     * In that case enable code below and disable next paragraph.
     *
     * Transport::getInstance()->getNotificationDismisserSender()->sendSignal(dismisserArgs,nsConsts::TTL_MAX);
     * if (status != ER_OK) {
     * QCC_LogError(status,"NotificationAsyncTaskEvents", "sendSignal failed.");
     * return;
     * }
     *
     * End of paragraph
     */

    /*
     * Paragraph to be disabled in case dismiss signal will not need to be sent via different object path each time
     */
    String appId;
    uint8_t* appIdBin;
    size_t len;
    m_AppIdArg.Get(AJPARAM_ARR_BYTE.c_str(), &len, &appIdBin);

    Conversions::ArrayOfBytesToString(&appIdBin, len, &appId);

    std::ostringstream stm;
    stm << abs(msgId);
    qcc::String objectPath = nsConsts::AJ_NOTIFICATION_DISMISSER_PATH + "/" + appId + "/" + std::string(stm.str()).c_str();
    NotificationDismisserSender notificationDismisserSender(Transport::getInstance()->getBusAttachment(), objectPath, status);
    if (status != ER_OK) {
        QCC_LogError(status, ("Could not create NotificationDismisserSender."));
    }
    status = Transport::getInstance()->getBusAttachment()->RegisterBusObject(notificationDismisserSender);
    if (status != ER_OK) {
        QCC_LogError(status, ("Could not register NotificationDismisserSender."));
    }
    QCC_DbgPrintf(("sendDismissSignal: going to send dismiss signal with object path %s", objectPath.c_str()));
    notificationDismisserSender.sendSignal(dismisserArgs, nsConsts::TTL_MAX);
    /*
     * End of paragraph
     */

    return status;
}
