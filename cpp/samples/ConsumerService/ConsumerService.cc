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

#include <iostream>
#include <sstream>
#include <signal.h>
#include <alljoyn/PasswordManager.h>
#include <alljoyn/notification/NotificationService.h>
#include <alljoyn/services_common/GenericLogger.h>
#include "../common/NotificationReceiverTestImpl.h"
#include "CommonSampleUtil.h"
#include <alljoyn/about/AnnouncementRegistrar.h>

#ifdef USE_SAMPLE_LOGGER
#include "../common/SampleLogger.h"
#endif

using namespace ajn;
using namespace services;

NotificationService* conService = 0;
NotificationReceiverTestImpl* Receiver = 0;
ajn::BusAttachment* busAttachment = 0;
static volatile sig_atomic_t s_interrupt = false;

#ifdef USE_SAMPLE_LOGGER
SampleLogger* myLogger;
#endif

void cleanup()
{
    std::cout << "cleanup() - start" << std::endl;
    if (conService)
        conService->shutdown();
    if (Receiver)
        delete Receiver;
    if (busAttachment)
        delete busAttachment;
#ifdef USE_SAMPLE_LOGGER
    if (myLogger)
        delete myLogger;
#endif
    std::cout << "cleanup() - end" << std::endl;
}

void signal_callback_handler(int32_t signum)
{
    std::cout << "got signal_callback_handler" << std::endl;
    cleanup();
    s_interrupt = true;
    std::cout << "Goodbye!" << std::endl;
    exit(signum);
}

bool WaitForSigInt(int32_t sleepTime) {
    if (s_interrupt == false) {
#ifdef _WIN32
        Sleep(100);
#else
        sleep(sleepTime);
#endif
        return false;
    }
    return true;
}

int main()
{
    std::string listOfApps;

    // Allow CTRL+C to end application
    signal(SIGINT, signal_callback_handler);

    std::cout << "Begin Consumer Application. (Press CTRL+C to end application)" << std::endl;
    std::cout << "Enter in a list of app names (separated by ';') you would like to receive notifications from." << std::endl;
    std::cout << "Empty list means all app names." << std::endl;
    std::getline(std::cin, listOfApps);

    // Initialize Service object and send it Notification Receiver object
    conService = NotificationService::getInstance();
//set Daemon passowrd only for bundled app
#ifdef QCC_USING_BD
    PasswordManager::SetCredentials("ALLJOYN_PIN_KEYX", "000000");
#endif

#ifdef USE_SAMPLE_LOGGER
    /* Optionally implement your own logger and instantiate it here */
    myLogger = new SampleLogger();
    conService->setLogger(myLogger);
#endif
    // change loglevel to debug:
    conService->getLogger()->setLogLevel(Log::LEVEL_DEBUG);

    Receiver = new NotificationReceiverTestImpl(NotificationReceiverTestImpl::ACTION_NOTHING);

    // Set the list of applications this receiver should receive notifications from
    Receiver->setApplications(listOfApps.c_str());

    busAttachment = CommonSampleUtil::prepareBusAttachment();
    if (busAttachment == NULL) {
        std::cout << "Could not initialize BusAttachment." << std::endl;
        cleanup();
        return 1;
    }
    QStatus status;

    status = conService->initReceive(busAttachment, Receiver);
    if (status != ER_OK) {
        std::cout << "Could not initialize receiver." << std::endl;
        cleanup();
        return 1;
    }

    status = CommonSampleUtil::addSessionlessMatch(busAttachment);
    if (status != ER_OK) {
        std::cout << "Could not add sessionless match." << std::endl;
        cleanup();
        return 1;
    }

    std::cout << "Waiting for notifications." << std::endl;

    int32_t sleepTime = 5;
    while (!WaitForSigInt(sleepTime)) ;



    return 0;
}