void BaselineBANMac::startup() {
    // Existing code...

    // New parameters for IEEE 802.15.6 MAC features
    int superframeDuration = 100; // Default duration in milliseconds (EAP + RAP + CAP)
    int EAPDuration = 10;         // Default duration of Enhanced Allocation Period (milliseconds)
    int RAPDuration = 20;         // Default duration of Reserved Access Period (milliseconds)
    int CAPDuration = 70;         // Default duration of Contention Access Period (milliseconds)

    int beaconInterval = 2000;    // Default beacon interval in milliseconds
    int beaconDuration = 50;      // Default beacon duration in milliseconds

    int priorityLevel = 2;        // Default priority level (P2 - Medium)
    string trafficCategory = "Dependent"; // Default traffic category

    int numFrequencyChannels = 16; // Default number of frequency channels for channel hopping
    int channelHoppingSequence[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}; // Default channel hopping sequence

    bool enableSleepModes = true; // Enable sleep modes for energy efficiency
    double sleepInterval = 10.0;  // Default sleep interval in seconds

    bool enableCoexistence = true; // Enable coexistence mechanisms

    // Existing code...

    // Assigning the new parameters
    superframeDuration = (double)par("superframeDuration") / 1000.0; // Convert milliseconds to seconds
    EAPDuration = (double)par("EAPDuration") / 1000.0;
    RAPDuration = (double)par("RAPDuration") / 1000.0;
    CAPDuration = (double)par("CAPDuration") / 1000.0;

    beaconInterval = par("beaconInterval");
    beaconDuration = par("beaconDuration");

    priorityLevel = getParentModule()->getParentModule()->getSubmodule("Application")->par("priority");
    trafficCategory = par("trafficCategory");

    numFrequencyChannels = par("numFrequencyChannels");
    channelHoppingSequence = new int[numFrequencyChannels];
    for (int i = 0; i < numFrequencyChannels; i++) {
        channelHoppingSequence[i] = par("channelHoppingSequence", i);
    }

    enableSleepModes = par("enableSleepModes");
    sleepInterval = (double)par("sleepInterval") / 1000.0; // Convert milliseconds to seconds

    enableCoexistence = par("enableCoexistence");

    // Existing code...
}


void BaselineBANMac::fromNetworkLayer(cPacket *pkt, int dst) {
    BaselineMacPacket *BaselineBANDataPkt = new BaselineMacPacket("BaselineBAN data packet", MAC_LAYER_PACKET);
    encapsulatePacket(BaselineBANDataPkt, pkt);

    // Determine the priority level and traffic category of the data packet
    int priorityLevel = BaselineBANDataPkt->getPriority(); // Assuming priority level is set in the BaselineMacPacket
    string trafficCategory = BaselineBANDataPkt->getTrafficCategory(); // Assuming traffic category is set in the BaselineMacPacket

    // Check if the packet can be buffered based on its priority and traffic category
    bool canBufferPacket = true; // Add logic to check if the buffer has space for the packet based on priority and category
    if (canBufferPacket) {
        if (priorityLevel == 7) {
            // High priority (P1 - Emergency) packet, transmit during EAP using TDMA
            if (isHub) {
                // Hub schedules the transmission slot for the node with UP7 in EAP
                scheduleEAPTransmission(BaselineBANDataPkt, dst);
            } else {
                // Sensor nodes transmit their UP7 packets during their scheduled EAP slot
                attemptTXDuringEAP(BaselineBANDataPkt);
            }
        } else if (priorityLevel >= 4 && priorityLevel <= 6) {
            // Medium priority (P2 - Dependent) packet, transmit during RAP using CSMA/CA
            if (isHub) {
                // Hub schedules the RAP slot for nodes with UP4, UP5, and UP6
                scheduleRAPTransmission(BaselineBANDataPkt, dst);
            } else {
                // Sensor nodes transmit their UP4, UP5, and UP6 packets during their scheduled RAP slot
                attemptTXDuringRAP(BaselineBANDataPkt);
            }
        } else {
            // Low priority (P3 - Independent) packet, transmit during CAP using CSMA/CA
            if (isHub) {
                // Hub schedules the CAP slot for nodes with UP0, UP1, UP2, and UP3
                scheduleCAPTransmission(BaselineBANDataPkt, dst);
            } else {
                // Sensor nodes transmit their UP0, UP1, UP2, and UP3 packets during their scheduled CAP slot
                attemptTXDuringCAP(BaselineBANDataPkt);
            }
        }
    } else {
        trace() << "WARNING BaselineBAN MAC buffer overflow";
        collectOutput("Data pkt breakdown", "Fail, buffer overflow");
    }
}


void BaselineBANMac::attemptTxInRAP() {
    // Check if the node is eligible to transmit in RAP based on priority level
    if (priorityLevel >= 4 && priorityLevel <= 6) {
        // Check if the channel is idle (carrier sensing)
        if (!isChannelBusy()) {
            // If the backoff counter is zero, initiate the backoff process
            if (backoffCounter == 0) {
                backoffCounter = 1 + genk_intrand(0, CW);
            }

            // Perform the backoff procedure (decrement backoffCounter)
            if (backoffCounter > 0) {
                backoffCounter--;

                // If the backoff counter reaches zero, transmit the packet
                if (backoffCounter == 0) {
                    // Check if the packet buffer has the packet to be sent
                    if (packetToBeSent != nullptr) {
                        // Attempt to transmit the packet during RAP
                        transmitPacketDuringRAP(packetToBeSent);
                    }
                }
            }
        } else {
            // Channel is busy, wait and retry later (exp backoff or contention window)
            double waitTime = expBackoffTime(); // Use exponential backoff or contention window mechanism
            setTimer(CARRIER_SENSING, waitTime);
        }
    } else {
        // Node is not eligible to transmit in RAP, do not attempt transmission
        trace() << "Node is not eligible to transmit in RAP. Priority Level: " << priorityLevel;
    }
}

// Function to transmit the packet during the Reserved Access Period (RAP)
void BaselineBANMac::transmitPacketDuringRAP(BaselineMacPacket* packet) {
    // Set the flag to indicate the node is attempting to transmit
    attemptingToTX = true;

    // Clear the backoff counter since transmission is initiated
    backoffCounter = 0;

    // Transmit the packet using CSMA/CA mechanism during RAP
    // ... (implement the actual transmission logic here)

    // Set a timer for ACK reception or retransmission if necessary
    double timeout = calculateTimeout(); // Calculate ACK timeout based on data rate and channel conditions
    setTimer(WAIT_FOR_ACK, timeout);
}


// Structure to store RAP transmission slots for nodes with medium priority (P2 - Dependent)
struct RAPTransmissionSlot {
    int nodeAddress;      // Sensor node address
    simtime_t startTime;  // Start time of the RAP transmission slot
    simtime_t endTime;    // End time of the RAP transmission slot
};

// List to store the RAP transmission slots
std::vector<RAPTransmissionSlot> rapTransmissionSlots;

// Function to schedule the Reserved Access Period (RAP) transmission slot
void BaselineBANMac::scheduleRAPTransmission(BaselineMacPacket* packet, int dstNodeAddress) {
    int priorityLevel = packet->getPriority(); // Assuming priority level is set in the BaselineMacPacket

    // Check if the priority level is medium (P2 - Dependent)
    if (priorityLevel >= 4 && priorityLevel <= 6) {
        // Calculate the duration of the RAP slot based on packet size and data rate
        simtime_t transmissionDuration = calculateTransmissionDuration(packet);

        // Find the destination node in the list of RAP transmission slots
        bool nodeFound = false;
        for (auto& slot : rapTransmissionSlots) {
            if (slot.nodeAddress == dstNodeAddress) {
                // Check if the slot duration needs to be extended to accommodate the new transmission
                if (simTime() + transmissionDuration > slot.endTime) {
                    slot.endTime = simTime() + transmissionDuration;
                }
                nodeFound = true;
                break;
            }
        }

        // If the destination node is not found in the list, add a new transmission slot for it
        if (!nodeFound) {
            RAPTransmissionSlot newSlot;
            newSlot.nodeAddress = dstNodeAddress;
            newSlot.startTime = simTime();
            newSlot.endTime = simTime() + transmissionDuration;
            rapTransmissionSlots.push_back(newSlot);
        }

        // Schedule the transmission of the packet during the RAP slot
        simtime_t delay = calculateRAPTransmissionDelay(dstNodeAddress); // Calculate the delay to the start of the RAP slot
        setTimer(SEND_RAP_PACKET, delay);
    } else {
        // Node is not eligible for RAP transmission, schedule for CAP or other periods
        // ...
    }
}

// Function to calculate the transmission duration based on packet size and data rate
simtime_t BaselineBANMac::calculateTransmissionDuration(BaselineMacPacket* packet) {
    int packetSize = packet->getPacketSize(); // Assuming the packet size is set in the BaselineMacPacket
    double dataRate = par("phyDataRate");    // Get the data rate from the module parameter

    // Calculate transmission duration using packet size and data rate
    double transmissionDuration = (packetSize * 8.0) / dataRate; // in seconds
    return SimTime(transmissionDuration, SIMTIME_S);
}

// Function to calculate the delay to the start of the RAP transmission slot for a specific node
simtime_t BaselineBANMac::calculateRAPTransmissionDelay(int dstNodeAddress) {
    for (const auto& slot : rapTransmissionSlots) {
        if (slot.nodeAddress == dstNodeAddress) {
            return slot.startTime - simTime();
        }
    }

    // Node not found in the list, return 0 as there might be a scheduling conflict or it's a new node
    return 0;
}

// Function to send the packet during the RAP transmission slot
void BaselineBANMac::sendRAPPacket() {
    if (packetToBeSent != nullptr) {
        int dstNodeAddress = packetToBeSent->getDestAddr();
        // Check if the destination node is eligible for RAP transmission
        bool nodeFound = false;
        for (const auto& slot : rapTransmissionSlots) {
            if (slot.nodeAddress == dstNodeAddress && simTime() >= slot.startTime && simTime() <= slot.endTime) {
                // Transmit the packet during the RAP slot
                transmitPacketDuringRAP(packetToBeSent);
                nodeFound = true;
                break;
            }
        }
        if (!nodeFound) {
            // Destination node is not scheduled for RAP transmission, handle as per custom MAC logic
            // ...
        }
    }
}


// Function to handle transmission attempts during the Enhanced Allocation Period (EAP)
void BaselineBANMac::attemptTxInEAP() {
    int priorityLevel = packetToBeSent->getPriority(); // Assuming priority level is set in the BaselineMacPacket

    // Check if the priority level is high (P1 - Emergency)
    if (priorityLevel == 7) {
        // Check if the channel is idle (carrier sensing)
        if (!isChannelBusy()) {
            // Perform the backoff procedure (if necessary, depends on your EAP mechanism)
            // ... (implement the appropriate backoff procedure for EAP)

            // If the backoff is successful (or not needed), transmit the packet during EAP
            if (backoffIsSuccessful()) {
                // Transmit the packet during the Enhanced Allocation Period (EAP)
                transmitPacketDuringEAP(packetToBeSent);
            }
        } else {
            // Channel is busy, wait and retry later (exp backoff or contention window)
            double waitTime = expBackoffTime(); // Use exponential backoff or contention window mechanism
            setTimer(CARRIER_SENSING, waitTime);
        }
    } else {
        // Node is not eligible to transmit in EAP, do not attempt transmission
        trace() << "Node is not eligible to transmit in EAP. Priority Level: " << priorityLevel;
    }
}

// Function to check if the backoff is successful (placeholder, adjust as per your custom MAC logic)
bool BaselineBANMac::backoffIsSuccessful() {
    return true; // Placeholder; implement your custom backoff logic based on EAP requirements
}

// Function to transmit the packet during the Enhanced Allocation Period (EAP)
void BaselineBANMac::transmitPacketDuringEAP(BaselineMacPacket* packet) {
    // Set the flag to indicate the node is attempting to transmit
    attemptingToTX = true;

    // Clear the backoff counter since transmission is initiated
    backoffCounter = 0;

    // Transmit the packet using TDMA mechanism during EAP
    // ... (implement the actual transmission logic for TDMA in EAP)

    // Set a timer for ACK reception or retransmission if necessary
    double timeout = calculateTimeout(); // Calculate ACK timeout based on data rate and channel conditions
    setTimer(WAIT_FOR_ACK, timeout);
}


// Structure to store EAP transmission slots for nodes with high priority (P1 - Emergency)
struct EAPTransmissionSlot {
    int nodeAddress;      // Sensor node address
    simtime_t startTime;  // Start time of the EAP transmission slot
    simtime_t endTime;    // End time of the EAP transmission slot
};

// List to store the EAP transmission slots
std::vector<EAPTransmissionSlot> eapTransmissionSlots;

// Function to schedule the Enhanced Allocation Period (EAP) transmission slot
void BaselineBANMac::scheduleEAPTransmission(BaselineMacPacket* packet, int dstNodeAddress) {
    int priorityLevel = packet->getPriority(); // Assuming priority level is set in the BaselineMacPacket

    // Check if the priority level is high (P1 - Emergency)
    if (priorityLevel == 7) {
        // Calculate the duration of the EAP slot based on packet size and data rate
        simtime_t transmissionDuration = calculateTransmissionDuration(packet);

        // Find the destination node in the list of EAP transmission slots
        bool nodeFound = false;
        for (auto& slot : eapTransmissionSlots) {
            if (slot.nodeAddress == dstNodeAddress) {
                // Check if the slot duration needs to be extended to accommodate the new transmission
                if (simTime() + transmissionDuration > slot.endTime) {
                    slot.endTime = simTime() + transmissionDuration;
                }
                nodeFound = true;
                break;
            }
        }

        // If the destination node is not found in the list, add a new transmission slot for it
        if (!nodeFound) {
            EAPTransmissionSlot newSlot;
            newSlot.nodeAddress = dstNodeAddress;
            newSlot.startTime = simTime();
            newSlot.endTime = simTime() + transmissionDuration;
            eapTransmissionSlots.push_back(newSlot);
        }

        // Schedule the transmission of the packet during the EAP slot
        simtime_t delay = calculateEAPTransmissionDelay(dstNodeAddress); // Calculate the delay to the start of the EAP slot
        setTimer(SEND_EAP_PACKET, delay);
    } else {
        // Node is not eligible for EAP transmission, schedule for CAP or other periods
        // ...
    }
}

// Function to calculate the transmission duration based on packet size and data rate
simtime_t BaselineBANMac::calculateTransmissionDuration(BaselineMacPacket* packet) {
    int packetSize = packet->getPacketSize(); // Assuming the packet size is set in the BaselineMacPacket
    double dataRate = par("phyDataRate");    // Get the data rate from the module parameter

    // Calculate transmission duration using packet size and data rate
    double transmissionDuration = (packetSize * 8.0) / dataRate; // in seconds
    return SimTime(transmissionDuration, SIMTIME_S);
}

// Function to calculate the delay to the start of the EAP transmission slot for a specific node
simtime_t BaselineBANMac::calculateEAPTransmissionDelay(int dstNodeAddress) {
    for (const auto& slot : eapTransmissionSlots) {
        if (slot.nodeAddress == dstNodeAddress) {
            return slot.startTime - simTime();
        }
    }

    // Node not found in the list, return 0 as there might be a scheduling conflict or it's a new node
    return 0;
}

// Function to send the packet during the EAP transmission slot
void BaselineBANMac::sendEAPPacket() {
    if (packetToBeSent != nullptr) {
        int dstNodeAddress = packetToBeSent->getDestAddr();
        // Check if the destination node is eligible for EAP transmission
        bool nodeFound = false;
        for (const auto& slot : eapTransmissionSlots) {
            if (slot.nodeAddress == dstNodeAddress && simTime() >= slot.startTime && simTime() <= slot.endTime) {
                // Transmit the packet during the EAP slot
                transmitPacketDuringEAP(packetToBeSent);
                nodeFound = true;
                break;
            }
        }
        if (!nodeFound) {
            // Destination node is not scheduled for EAP transmission, handle as per custom MAC logic
            // ...
        }
    }
}

// Function to handle transmission attempts during the Contention Access Period (CAP)
void BaselineBANMac::attemptTxInCAP() {
    int priorityLevel = packetToBeSent->getPriority(); // Assuming priority level is set in the BaselineMacPacket

    // Check if the priority level is low (P3 - Independent)
    if (priorityLevel >= 0 && priorityLevel <= 3) {
        // Check if the channel is idle (carrier sensing)
        if (!isChannelBusy()) {
            // Perform the backoff procedure (if necessary, depends on your CAP mechanism)
            // ... (implement the appropriate backoff procedure for CAP)

            // If the backoff is successful (or not needed), transmit the packet during CAP
            if (backoffIsSuccessful()) {
                // Transmit the packet during the Contention Access Period (CAP)
                transmitPacketDuringCAP(packetToBeSent);
            }
        } else {
            // Channel is busy, wait and retry later (exp backoff or contention window)
            double waitTime = expBackoffTime(); // Use exponential backoff or contention window mechanism
            setTimer(CARRIER_SENSING, waitTime);
        }
    } else {
        // Node is not eligible to transmit in CAP, do not attempt transmission
        trace() << "Node is not eligible to transmit in CAP. Priority Level: " << priorityLevel;
    }
}

// Function to check if the backoff is successful (placeholder, adjust as per your custom MAC logic)
bool BaselineBANMac::backoffIsSuccessful() {
    return true; // Placeholder; implement your custom backoff logic based on CAP requirements
}

// Function to transmit the packet during the Contention Access Period (CAP)
void BaselineBANMac::transmitPacketDuringCAP(BaselineMacPacket* packet) {
    // Set the flag to indicate the node is attempting to transmit
    attemptingToTX = true;

    // Clear the backoff counter since transmission is initiated
    backoffCounter = 0;

    // Transmit the packet using CSMA/CA mechanism during CAP
    // ... (implement the actual transmission logic for CSMA/CA in CAP)

    // Set a timer for ACK reception or retransmission if necessary
    double timeout = calculateTimeout(); // Calculate ACK timeout based on data rate and channel conditions
    setTimer(WAIT_FOR_ACK, timeout);
}

// Structure to store CAP transmission slots for nodes with low priority (P3 - Independent)
struct CAPTransmissionSlot {
    int nodeAddress;      // Sensor node address
    simtime_t startTime;  // Start time of the CAP transmission slot
    simtime_t endTime;    // End time of the CAP transmission slot
};

// List to store the CAP transmission slots
std::vector<CAPTransmissionSlot> capTransmissionSlots;

// Function to schedule the Contention Access Period (CAP) transmission slot
void BaselineBANMac::scheduleCAPTransmission(BaselineMacPacket* packet, int dstNodeAddress) {
    int priorityLevel = packet->getPriority(); // Assuming priority level is set in the BaselineMacPacket

    // Check if the priority level is low (P3 - Independent)
    if (priorityLevel >= 0 && priorityLevel <= 3) {
        // Calculate the duration of the CAP slot based on packet size and data rate
        simtime_t transmissionDuration = calculateTransmissionDuration(packet);

        // Find the destination node in the list of CAP transmission slots
        bool nodeFound = false;
        for (auto& slot : capTransmissionSlots) {
            if (slot.nodeAddress == dstNodeAddress) {
                // Check if the slot duration needs to be extended to accommodate the new transmission
                if (simTime() + transmissionDuration > slot.endTime) {
                    slot.endTime = simTime() + transmissionDuration;
                }
                nodeFound = true;
                break;
            }
        }

        // If the destination node is not found in the list, add a new transmission slot for it
        if (!nodeFound) {
            CAPTransmissionSlot newSlot;
            newSlot.nodeAddress = dstNodeAddress;
            newSlot.startTime = simTime();
            newSlot.endTime = simTime() + transmissionDuration;
            capTransmissionSlots.push_back(newSlot);
        }

        // Schedule the transmission of the packet during the CAP slot
        simtime_t delay = calculateCAPTransmissionDelay(dstNodeAddress); // Calculate the delay to the start of the CAP slot
        setTimer(SEND_CAP_PACKET, delay);
    } else {
        // Node is not eligible for CAP transmission, schedule for other periods
        // ...
    }
}

// Function to calculate the transmission duration based on packet size and data rate
simtime_t BaselineBANMac::calculateTransmissionDuration(BaselineMacPacket* packet) {
    int packetSize = packet->getPacketSize(); // Assuming the packet size is set in the BaselineMacPacket
    double dataRate = par("phyDataRate");    // Get the data rate from the module parameter

    // Calculate transmission duration using packet size and data rate
    double transmissionDuration = (packetSize * 8.0) / dataRate; // in seconds
    return SimTime(transmissionDuration, SIMTIME_S);
}

// Function to calculate the delay to the start of the CAP transmission slot for a specific node
simtime_t BaselineBANMac::calculateCAPTransmissionDelay(int dstNodeAddress) {
    for (const auto& slot : capTransmissionSlots) {
        if (slot.nodeAddress == dstNodeAddress) {
            return slot.startTime - simTime();
        }
    }

    // Node not found in the list, return 0 as there might be a scheduling conflict or it's a new node
    return 0;
}

// Function to send the packet during the CAP transmission slot
void BaselineBANMac::sendCAPPacket() {
    if (packetToBeSent != nullptr) {
        int dstNodeAddress = packetToBeSent->getDestAddr();
        // Check if the destination node is eligible for CAP transmission
        bool nodeFound = false;
        for (const auto& slot : capTransmissionSlots) {
            if (slot.nodeAddress == dstNodeAddress && simTime() >= slot.startTime && simTime() <= slot.endTime) {
                // Transmit the packet during the CAP slot
                transmitPacketDuringCAP(packetToBeSent);
                nodeFound = true;
                break;
            }
        }
        if (!nodeFound) {
            // Destination node is not scheduled for CAP transmission, handle as per custom MAC logic
            // ...
        }
    }
}


void BaselineBANMac::fromRadioLayer(cPacket *pkt, double rssi, double lqi) {
    // If the incoming packet is not BaselineBAN, return (VirtualMAC will delete it)
    BaselineMacPacket *BaselineBANPkt = dynamic_cast<BaselineMacPacket*>(pkt);
    if (BaselineBANPkt == NULL) return;

    // Filter the incoming BaselineBAN packet
    if (!isPacketForMe(BaselineBANPkt)) return;

    /* Handle data packets */
    if (BaselineBANPkt->getFrameType() == DATA) {
        toNetworkLayer(decapsulatePacket(BaselineBANPkt));
        /* If this pkt requires a block ACK, we should send it,
         * by looking at what packet we have received (NOT IMPLEMENTED) */
        // NOT IMPLEMENTED

        // Handle future polls (I_ACK_POLL)
        if (BaselineBANPkt->getAckPolicy() == I_ACK_POLICY) {
            BaselineMacPacket *ackPacket = new BaselineMacPacket("ACK packet", MAC_LAYER_PACKET);
            setHeaderFields(ackPacket, N_ACK_POLICY, CONTROL, (sendIAckPoll ? I_ACK_POLL : I_ACK));
            ackPacket->setNID(BaselineBANPkt->getNID());
            ackPacket->setByteLength(BASELINEBAN_HEADER_SIZE);

            // If we are unconnected, set a proper HID (the packet is for us since it was not filtered)
            if (connectedHID == UNCONNECTED) {
                ackPacket->setHID(BaselineBANPkt->getHID());
            }

            // Set the appropriate fields if this is an I_ACK_POLL
            if (sendIAckPoll) {
                // We are sending a future poll
                ackPacket->setMoreData(1);
                sendIAckPoll = false;

                if (!naivePollingScheme) {
                    // If this node was not given a future poll already, update the hubPollTimers and nextFuturePollSlot.
                    // Also, if the hubPollTimers is empty, schedule the timer to send this first POLL
                    if (hubPollTimers.empty() || hubPollTimers.back().NID != BaselineBANPkt->getNID()) {
                        if (hubPollTimers.empty()) {
                            setTimer(SEND_POLL, frameStartTime + (nextFuturePollSlot - 1) * allocationSlotLength - getClock());
                        }

                        TimerInfo t;
                        t.NID = BaselineBANPkt->getNID();
                        t.slotsGiven = 1;
                        t.endSlot = nextFuturePollSlot;
                        hubPollTimers.push(t);
                        nextFuturePollSlot++;
                        lastTxAccessSlot[t.NID].polled = t.endSlot;
                    }
                }

                int futurePollSlot = (naivePollingScheme ? nextFuturePollSlot : hubPollTimers.back().endSlot);
                trace() << "Future POLL at slot " << futurePollSlot << " inserted in ACK packet";
                ackPacket->setSequenceNumber(futurePollSlot);
            }

            trace() << "Transmitting ACK to/from NID:" << BaselineBANPkt->getNID();
            toRadioLayer(ackPacket);
            toRadioLayer(createRadioCommand(SET_STATE, TX));
            isRadioSleeping = false;

            // Any future attempts to TX should be done AFTER we are finished TXing the I-ACK.
            // Set the appropriate timer and variable.
            // BASELINEBAN_HEADER_SIZE is the size of the ACK. 2*pTIFS is explained at sendPacket()
            setTimer(START_ATTEMPT_TX, (TX_TIME(BASELINEBAN_HEADER_SIZE) + 2 * pTIFS));
            futureAttemptToTX = true;
        }
    }
    /* If this was a data packet, we have done all our processing
	 * (+ sending a possible I-ACK or I-ACK-POLL), so just return.
	 */
	if (BaselineBANPkt->getFrameType() == DATA) return;

	/* Handle management and control packets */
	switch(BaselineBANPkt->getFrameSubtype()) {
        case BEACON: {
    BaselineBeaconPacket * BaselineBANBeacon = check_and_cast<BaselineBeaconPacket*>(BaselineBANPkt);
    simtime_t beaconTxTime = TX_TIME(BaselineBANBeacon->getByteLength()) + pTIFS;

    // Store the time the frame starts. Needed for polls and posts, which only reference end allocation slot
    frameStartTime = getClock() - beaconTxTime;

    // Get the allocation slot length, which is used in many calculations
    allocationSlotLength = BaselineBANBeacon->getAllocationSlotLength() / 1000.0;
    SInominal = (allocationSlotLength / 10.0 - pTIFS) / (2 * mClockAccuracy);

    // A beacon is our synchronization event. Update relevant timer
    pastSyncIntervalNominal = false;
    setTimer(SYNC_INTERVAL_TIMEOUT, SInominal);

    beaconPeriodLength = BaselineBANBeacon->getBeaconPeriodLength();
    RAP1Length = BaselineBANBeacon->getRAP1Length();

    // Determine the user priority based on the node's characteristics
    int userPriority = getUserPriority(); // Implement your own function to determine user priority (p1, p2, p3)

    // Check the user priority to handle different superframes
    if (userPriority == 7) {
        // User priority p1 (UP7) - High priority, EAP superframe
        trace() << "State from " << macState << " to MAC_EAP";
        macState = MAC_EAP;
        endTime = getClock() + EAPLength * allocationSlotLength - beaconTxTime;
    } else if (userPriority >= 4 && userPriority <= 6) {
        // User priority p2 (UP4, UP5, UP6) - Medium priority, RAP superframe
        trace() << "State from " << macState << " to MAC_RAP";
        macState = MAC_RAP;
        endTime = getClock() + RAP1Length * allocationSlotLength - beaconTxTime;
    } else {
        // User priority p3 (UP0, UP1, UP2, UP3) - Low priority, CAP superframe
        trace() << "State from " << macState << " to MAC_CAP";
        macState = MAC_CAP;
        endTime = getClock() + CAP1Length * allocationSlotLength - beaconTxTime;
    }

    collectOutput("Beacons received");
    trace() << "Beacon rx: reseting sync clock to " << SInominal << " secs";
    trace() << "           Slot= " << allocationSlotLength << " secs, beacon period= " << beaconPeriodLength << " slots";
    trace() << "           RAP1= " << RAP1Length << " slots, RAP ends at time: " << endTime;

    /* Flush the Management packets buffer. Delete packetToBeSent if it is a management packet
     * This is a design choice. It simplifies the flowcontrol and prevents rare cases where
     * management packets are piled up. More complicated schemes chould be applied w.r.t.
     * connection requests and connection assignments.
     */
    if (packetToBeSent != nullptr && packetToBeSent->getFrameType() != DATA) {
        cancelAndDelete(packetToBeSent);
        packetToBeSent = nullptr;
    }

    while (!MgmtBuffer.empty()) {
        cancelAndDelete(MgmtBuffer.front());
        MgmtBuffer.pop();
    }

    // Check if the node is connected to the hub
    if (connectedHID == UNCONNECTED) {
        // Go into a setup phase again after this beacon's RAP
        setTimer(START_SETUP, RAP1Length * allocationSlotLength - beaconTxTime);
        trace() << "(unconnected): Go back to setup mode when RAP ends";

        // We will try to connect to this BAN if our scheduled access length is NOT set to unconnected (-1)
        if (scheduledAccessLength >= 0) {
            // We are unconnected, and we need to connect to obtain scheduled access
            // Create and send a connection request
            BaselineConnectionRequestPacket *connectionRequest = new BaselineConnectionRequestPacket("BaselineBAN connection request packet", MAC_LAYER_PACKET);

            // This block takes care of general header fields
            setHeaderFields(connectionRequest, I_ACK_POLICY, MANAGEMENT, CONNECTION_REQUEST);
            // While setHeaderFields should take care of the HID field, we are currently unconnected.
            // We want to keep this state, yet send the request to the right hub.
            connectionRequest->setHID(BaselineBANBeacon->getHID());

            // This block takes care of connection request specific fields
            connectionRequest->setRecipientAddress(BaselineBANBeacon->getSenderAddress());
            connectionRequest->setSenderAddress(SELF_MAC_ADDRESS);
            // In this implementation, our schedule always starts from the next beacon
            connectionRequest->setNextWakeup(BaselineBANBeacon->getSequenceNumber() + 1);
            connectionRequest->setWakeupInterval(scheduledAccessPeriod);
            // Uplink request is simplified in this implementation to only ask for a number of slots needed
            connectionRequest->setUplinkRequest(scheduledAccessLength);
            connectionRequest->setByteLength(BASELINEBAN_CONNECTION_REQUEST_SIZE);

            // Management packets go in their own buffer, and handled by attemptTX() with priority
            MgmtBuffer.push(connectionRequest);
            trace() << "(unconnected): Created connection request";
        }
    } else {
        // Schedule a timer to wake up for the next beacon (it might be m periods away)
        setTimer(WAKEUP_FOR_BEACON, beaconPeriodLength * scheduledAccessPeriod * allocationSlotLength - beaconTxTime - GUARD_TIME);

        // If we have a schedule that does not start immediately after RAP, or our schedule
        // is not assigned yet, then go to sleep after RAP.
        if ((scheduledTxAccessStart == UNCONNECTED && RAP1Length < beaconPeriodLength)
                || (scheduledTxAccessStart - 1 > RAP1Length)) {
            setTimer(START_SLEEPING, RAP1Length * allocationSlotLength - beaconTxTime);
            trace() << "--- Start sleeping in: " << RAP1Length * allocationSlotLength - beaconTxTime << " secs";
        }

        // Schedule the timer to go in scheduled TX access, IF we have a valid schedule
        if (scheduledTxAccessEnd > scheduledTxAccessStart) {
            setTimer(START_SCHEDULED_TX_ACCESS, (scheduledTxAccessStart - 1) * allocationSlotLength - beaconTxTime + GUARD_TX_TIME);
            trace() << "--- Start scheduled TX access in: " << (scheduledTxAccessStart - 1) * allocationSlotLength - beaconTxTime + GUARD_TX_TIME << " secs";
        }

        // We should also handle the case when we have a scheduled RX access. This is not implemented yet.
    }

    attemptTX();
    break;
}

 case I_ACK_POLL: {
            handlePoll(BaselineBANPkt);
            // Roll over to the ACK part
            // No break statement here to proceed to the next case (I_ACK)
        }

        case I_ACK: {
            waitingForACK = false;
            cancelTimer(ACK_TIMEOUT);

            if (packetToBeSent == NULL || currentPacketTransmissions == 0) {
                trace() << "WARNING: Received I-ACK with packetToBeSent being NULL, or not TXed!";
                break;
            }

            // Collect statistics
            int priority = getPacketPriority(packetToBeSent);
            if (currentPacketTransmissions == 1) {
                if (priority == PRIORITY_P1)
                    collectOutput("Data pkt breakdown (P1)", "Success, 1st try");
                else if (priority == PRIORITY_P2)
                    collectOutput("Data pkt breakdown (P2)", "Success, 1st try");
                else if (priority == PRIORITY_P3)
                    collectOutput("Data pkt breakdown (P3)", "Success, 1st try");
                else
                    collectOutput("Mgmt & Ctrl pkt breakdown", "Success, 1st try");
            } else {
                if (priority == PRIORITY_P1)
                    collectOutput("Data pkt breakdown (P1)", "Success, 2 or more tries");
                else if (priority == PRIORITY_P2)
                    collectOutput("Data pkt breakdown (P2)", "Success, 2 or more tries");
                else if (priority == PRIORITY_P3)
                    collectOutput("Data pkt breakdown (P3)", "Success, 2 or more tries");
                else
                    collectOutput("Mgmt & Ctrl pkt breakdown", "Success, 2 or more tries");
            }

            cancelAndDelete(packetToBeSent);
            packetToBeSent = NULL;
            currentPacketTransmissions = 0;
            currentPacketCSFails = 0;

            // Update CW based on the priority of the next packet to be sent
            int nextPriority = getNextPacketPriority();
            CW = CWmin[nextPriority];

            // We could handle future posts here (if packet not I_ACK_POLL and moreData > 0)
            attemptTX();
            break;
        }

        case B_ACK_POLL: {
    handlePoll(BaselineBANPkt);
    // Roll over to the ACK part (no additional code needed here)
    break;
}

case B_ACK: {
    waitingForACK = false;
    cancelTimer(ACK_TIMEOUT);

    // Clean up the packetToBeSent and related variables
    if (packetToBeSent != NULL) {
        cancelAndDelete(packetToBeSent);
        packetToBeSent = NULL;
    }
    currentPacketTransmissions = 0;
    currentPacketCSFails = 0;
    CW = CWmin[priority];

    // TODO: Analyze the bitmap and see if some of the LACK packets need to be retransmitted

    // Attempt transmission of new packets
    attemptTX();
    break;
}

case CONNECTION_ASSIGNMENT: {
    BaselineConnectionAssignmentPacket *connAssignment = check_and_cast<BaselineConnectionAssignmentPacket*>(BaselineBANPkt);
    if (connAssignment->getStatusCode() == ACCEPTED || connAssignment->getStatusCode() == MODIFIED) {
        connectedHID = connAssignment->getHID();
        connectedNID = connAssignment->getAssignedNID();
        // Set anew the header fields of the packet to be sent
        if (packetToBeSent) {
            packetToBeSent->setHID(connectedHID);
            packetToBeSent->setNID(connectedNID);
        }
        // Set the start and end times for the schedule
        scheduledTxAccessStart = connAssignment->getUplinkRequestStart();
        scheduledTxAccessEnd = connAssignment->getUplinkRequestEnd();
        trace() << "connected as NID " << connectedNID << "  --start TX access at slot: " << scheduledTxAccessStart << ", end at slot: " << scheduledTxAccessEnd;
    } else {
        // The connection request is rejected, handle it according to your requirements
        trace() << "Connection Request REJECTED, status code: " << connAssignment->getStatusCode();
        // TODO: Handle the rejected connection request, if needed
    }
    break;
}

case DISCONNECTION: {
    // Handle disconnection
    connectedHID = UNCONNECTED;
    connectedNID = UNCONNECTED;
    // TODO: Handle any additional tasks or cleanups required upon disconnection
    break;
}

case CONNECTION_REQUEST: {
    BaselineConnectionRequestPacket *connRequest = check_and_cast<BaselineConnectionRequestPacket*>(BaselineBANPkt);

    // Create the connection assignment packet
    BaselineConnectionAssignmentPacket *connAssignment = new BaselineConnectionAssignmentPacket("BaselineBAN connection assignment", MAC_LAYER_PACKET);
    setHeaderFields(connAssignment, I_ACK_POLICY, MANAGEMENT, CONNECTION_ASSIGNMENT);

    // Get the full ID of the requesting node
    int fullAddress = connRequest->getSenderAddress();

    // Check if the request is on an already active assignment
    map<int, slotAssign_t>::iterator iter = slotAssignmentMap.find(fullAddress);
    if (iter != slotAssignmentMap.end()) {
        // The request has been processed *successfully* before, assign old resources
        connAssignment->setStatusCode(ACCEPTED);
        connAssignment->setAssignedNID(iter->second.NID);
        connAssignment->setUplinkRequestStart(iter->second.startSlot);
        connAssignment->setUplinkRequestEnd(iter->second.endSlot);
        trace() << "Connection request seen before! Assigning stored NID and resources...";
        trace() << "Connection request from NID " << connRequest->getNID() << " (full addr: " << fullAddress << ") Assigning connected NID " << iter->second.NID;
    } else {
        // The request has not been processed before, try to assign new resources
        if (connRequest->getUplinkRequest() > beaconPeriodLength - (currentFirstFreeSlot - 1)) {
            connAssignment->setStatusCode(REJ_NO_RESOURCES);
            // Can not accommodate the request, no available resources
        } else if (currentFreeConnectedNID > 239) {
            connAssignment->setStatusCode(REJ_NO_NID);
            // No available NIDs for new connections
        } else {
            // Update the slotAssignmentMap with the new assignment
            slotAssign_t newAssignment;
            newAssignment.NID = currentFreeConnectedNID;
            newAssignment.startSlot = currentFirstFreeSlot;
            newAssignment.endSlot = currentFirstFreeSlot + connRequest->getUplinkRequest();
            slotAssignmentMap[fullAddress] = newAssignment;

            // Construct the rest of the connection assignment packet
            connAssignment->setStatusCode(ACCEPTED);
            connAssignment->setAssignedNID(newAssignment.NID);
            connAssignment->setUplinkRequestStart(newAssignment.startSlot);
            connAssignment->setUplinkRequestEnd(newAssignment.endSlot);
            trace() << "Connection request from NID " << connRequest->getNID() << " (full addr: " << fullAddress << ") Assigning connected NID " << newAssignment.NID;

            // Update hub's lastTxAccessSlot and currentFreeConnectedNID
            lastTxAccessSlot[currentFreeConnectedNID].scheduled = newAssignment.endSlot - 1;
            currentFirstFreeSlot += connRequest->getUplinkRequest();
            currentFreeConnectedNID++;
        }
    }

    // Push the connection assignment packet to the Management packets buffer
    MgmtBuffer.push(connAssignment);

    // Transmission will be attempted after we are done sending the I-ACK
    trace() << "Connection assignment created, wait for " << (TX_TIME(BASELINEBAN_HEADER_SIZE) + 2 * pTIFS) << " to attemptTX";
    break;
}

case T_POLL:
    // Just read the time values from the payload, update relevant variables, and roll over to handle the POLL part (no break)
case POLL: {
    handlePoll(BaselineBANPkt);
    break;
}

case ASSOCIATION:
case DISASSOCIATION:
case PTK:
case GTK: {
    trace() << "WARNING: unimplemented packet subtype in [" << BaselineBANPkt->getName() << "]";
    // Handle unimplemented packet subtype according to your requirements
    break;
}



}


/* The specific finish function for BaselineBANMAC does needed cleanup when simulation ends
 */
void BaselineBANMac::finishSpecific(){
	if (packetToBeSent != NULL) cancelAndDelete(packetToBeSent);
	while(!MgmtBuffer.empty()) {
		cancelAndDelete(MgmtBuffer.front());
		MgmtBuffer.pop();
    }
    if (isHub) {delete[] reqToSendMoreData; delete[] lastTxAccessSlot;}
}

bool BaselineBANMac::isPacketForMe(BaselineMacPacket *pkt) {
    int pktUP = pkt->getUserPriority(); // Assuming you have a method to retrieve the User Priority from the packet
    int pktNID = pkt->getNID(); // Assuming you have a method to retrieve the Node ID from the packet

    // Check if the packet is addressed to the hub (User Priority 7)
    if (pktUP == UP7) {
        if (isHub) return true; // The hub can receive packets with UP7
        return false; // Sensors cannot receive UP7 packets
    }

    // Check if the packet is addressed to the sensors (User Priority 6, 5, 4, 3, 2, 1, or 0)
    if (pktUP >= UP0 && pktUP <= UP6) {
        // Check if the packet is a broadcast packet
        if (pktNID == BROADCAST_NID) {
            if (isHub) return true; // The hub can receive broadcast packets
            return false; // Sensors cannot receive broadcast packets
        }

        // Check if the packet is addressed to the hub
        if (pktNID == connectedNID) {
            if (isHub) return true; // The hub can receive packets addressed to itself
            return false; // Sensors cannot receive packets addressed to the hub
        }

        // Check if the packet is addressed to any of the sensors (single-hop communication)
        if (!isHub && pktNID >= 1 && pktNID <= 5) {
            return true;
        }
    }

    // For all other cases, the packet is not for this node
    return false;
}


/* A function to calculate the extra guard time, if we are past the Sync time nominal.
 */
simtime_t BaselineBANMac::extraGuardTime() {
	return (simtime_t) (getClock() - syncIntervalAdditionalStart) * mClockAccuracy;
}

void BaselineBANMac::setHeaderFields(BaselineMacPacket *pkt, AcknowledgementPolicy_type ackPolicy, Frame_type frameType, Frame_subtype frameSubtype, int userPriority) {
    pkt->setHID(connectedHID);
    if (connectedNID != UNCONNECTED)
        pkt->setNID(connectedNID);
    else
        pkt->setNID(unconnectedNID);

    pkt->setAckPolicy(ackPolicy);
    pkt->setFrameType(frameType);
    pkt->setFrameSubtype(frameSubtype);
    pkt->setUserPriority(userPriority); // Set the user priority based on the requirements

    // Determine the moreData flag based on the packet type and role (hub or sensor)
    if (frameType == DATA && !isHub) {
        // Sensors need to check their buffers for more data
        if (TXBuffer.size() != 0 || MgmtBuffer.size() != 0) {
            // Option to enhance BaselineBAN by sending how many more packets we have
            if (enhanceMoreData)
                pkt->setMoreData(TXBuffer.size() + MgmtBuffer.size());
            else
                pkt->setMoreData(1);
        }
    } else if (frameType == DATA && isHub) {
        // Hubs need to handle their moreData flag (signaling posts) separately
        // Set the appropriate moreData value for the hub (e.g., based on its buffers)
        pkt->setMoreData(...); // Replace ... with the actual logic to set moreData for the hub
    } else {
        // For non-DATA packets, set moreData to 0
        pkt->setMoreData(0);
    }
}

void BaselineBANMac::attemptTX() {
    // If we are not in an appropriate state, return
    if (macState != MAC_RAP && macState != MAC_FREE_TX_ACCESS) return;
    /* if we are currently attempting to TX or we have scheduled a future
     * attempt to TX, or waiting for an ack, return
     */
    if (waitingForACK || attemptingToTX || futureAttemptToTX) return;

    // Check if there's a packet to be sent and if it has exceeded the maximum packet tries
    if (packetToBeSent && currentPacketTransmissions + currentPacketCSFails < maxPacketTries) {
        if (macState == MAC_RAP && (enableRAP || packetToBeSent->getFrameType() != DATA))
            attemptTxInRAP();
        if (macState == MAC_FREE_TX_ACCESS && canFitTx())
            sendPacket();
        return;
    }

    // If there is still a packet in the buffer after max tries, delete it, reset relevant variables, and collect stats
    if (packetToBeSent) {
        trace() << "Max TX attempts reached. Last attempt was a CS fail";
        if (currentPacketCSFails == maxPacketTries) {
            if (packetToBeSent->getFrameType() == DATA)
                collectOutput("Data pkt breakdown", "Failed, Channel busy");
            else
                collectOutput("Mgmt & Ctrl pkt breakdown", "Failed, Channel busy");
        } else {
            if (packetToBeSent->getFrameType() == DATA)
                collectOutput("Data pkt breakdown", "Failed, No Ack");
            else
                collectOutput("Mgmt & Ctrl pkt breakdown", "Failed, No Ack");
        }
        cancelAndDelete(packetToBeSent);
        packetToBeSent = NULL;
        currentPacketTransmissions = 0;
        currentPacketCSFails = 0;
    }

    // Try to draw a new packet from the Management buffer based on traffic priority
    if (!MgmtBuffer.empty()) {
        BaselineMacPacket* nextPacket = (BaselineMacPacket*)MgmtBuffer.front();
        int userPriority = nextPacket->getUserPriority();
        if (userPriority == HIGH_TRAFFIC_PRIORITY) {
            // Handle packets with high traffic priority first
            packetToBeSent = nextPacket;
            MgmtBuffer.pop();
        } else if (userPriority == MEDIUM_TRAFFIC_PRIORITY) {
            // If there are no high-priority packets, handle packets with medium traffic priority
            if (!hasHighPriorityPackets(MgmtBuffer)) {
                packetToBeSent = nextPacket;
                MgmtBuffer.pop();
            }
        } else {
            // If there are no high-priority or medium-priority packets, handle low-priority packets
            if (!hasHighPriorityPackets(MgmtBuffer) && !hasMediumPriorityPackets(MgmtBuffer)) {
                packetToBeSent = nextPacket;
                MgmtBuffer.pop();
            }
        }
    } else if (connectedNID != UNCONNECTED && !TXBuffer.empty()) {
        // If there are no packets in the Management buffer, draw a packet from the Data buffer
        packetToBeSent = (BaselineMacPacket*)TXBuffer.front();
        TXBuffer.pop();
        setHeaderFields(packetToBeSent, I_ACK_POLICY, DATA, RESERVED, LOW_TRAFFIC_PRIORITY);
    }

    // If we found a packet in any of the buffers, try to TX it
    if (packetToBeSent) {
        if (macState == MAC_RAP && (enableRAP || packetToBeSent->getFrameType() != DATA))
            attemptTxInRAP();
        if (macState == MAC_FREE_TX_ACCESS && canFitTx())
            sendPacket();
    }
}


bool BaselineBANMac::canFitTx() {
    if (!packetToBeSent) return false;

    // Calculate the transmission time for the current packet
    double txTime = TX_TIME(packetToBeSent->getByteLength()) + pTIFS;

    // Check if the transmission can fit in EAP superframe
    if (macState == MAC_EAP) {
        if (endTime - getClock() - (GUARD_FACTOR * GUARD_TIME) - txTime > 0)
            return true;
    }
    // Check if the transmission can fit in RAP superframe
    else if (macState == MAC_RAP) {
        if (endTime - getClock() - (GUARD_FACTOR * GUARD_TIME) - txTime > 0)
            return true;
    }
    // Check if the transmission can fit in CAP superframe
    else if (macState == MAC_CAP) {
        if (endTime - getClock() - (GUARD_FACTOR * GUARD_TIME) - txTime > 0)
            return true;
    }

    return false;
}

// Define the superframe periods and their durations
enum SuperframePeriod {
    EAP_PERIOD, // TDMA period for P1 (UP=7) packets (Emergency category)
    RAP_PERIOD, // CSMA/CA period for P2 (UP4, UP5, UP6) packets (Dependent category)
    CAP_PERIOD, // CSMA/CA period for P3 (UP0, UP1, UP2, UP3) packets (Independent category)
};

// Define the priority levels
enum PriorityLevel {
    PRIORITY_P1, // High priority (UP=7)
    PRIORITY_P2, // Medium priority (UP4, UP5, UP6)
    PRIORITY_P3, // Low priority (UP0, UP1, UP2, UP3)
};

// Function to determine the current superframe period
SuperframePeriod getCurrentSuperframePeriod() {
    // Implement your logic to determine the current superframe period based on time or other factors
    // This function can use timers or other mechanisms to switch between periods
    // For simplicity, let's assume a static period schedule:
    // EAP_PERIOD for the first 5 seconds, RAP_PERIOD for the next 5 seconds, and then CAP_PERIOD.
    if (getCurrentTime() < 5) {
        return EAP_PERIOD;
    } else if (getCurrentTime() < 10) {
        return RAP_PERIOD;
    } else {
        return CAP_PERIOD;
    }
}

// Function to check if the current time falls within the superframe period
bool isWithinSuperframePeriod(SuperframePeriod period) {
    // Implement your logic to check if the current time is within the specified superframe period
    // For simplicity, let's assume that the superframe periods are of fixed duration.
    // In practice, you may use timers or other mechanisms to track the superframe periods.
    return (getCurrentTime() >= 0 && getCurrentTime() < 5) && period == EAP_PERIOD ||
           (getCurrentTime() >= 5 && getCurrentTime() < 10) && period == RAP_PERIOD ||
           (getCurrentTime() >= 10) && period == CAP_PERIOD;
}

// Function to send a packet based on the current superframe period and priority level
void sendPacket() {
    SuperframePeriod currentPeriod = getCurrentSuperframePeriod();

    // Determine the priority level of the packet (P1, P2, P3) based on its UP (User Priority) and traffic category
    PriorityLevel packetPriority = determinePacketPriority(); // Implement your logic here

    if (currentPeriod == EAP_PERIOD) {
        // During EAP period, use TDMA to send high-priority packets (P1)
        if (packetPriority == PRIORITY_P1) {
            // Use TDMA to schedule packet transmission for P1 (UP=7) packets (Emergency category)
            // Wait for the TDMA slot and send the packet
            sendUsingTDMA();
        }
    } else if (currentPeriod == RAP_PERIOD) {
        // During RAP period, use CSMA/CA to send medium-priority packets (P2)
        if (packetPriority == PRIORITY_P2) {
            // Use CSMA/CA to contend for the channel and send the packet
            sendUsingCSMACA();
        }
    } else if (currentPeriod == CAP_PERIOD) {
        // During CAP period, use CSMA/CA to send low-priority packets (P3)
        if (packetPriority == PRIORITY_P3) {
            // Use CSMA/CA to contend for the channel and send the packet
            sendUsingCSMACA();
        }
    }
}

// Define the priority levels
enum PriorityLevel {
    PRIORITY_P1, // High priority (UP=7)
    PRIORITY_P2, // Medium priority (UP4, UP5, UP6)
    PRIORITY_P3, // Low priority (UP0, UP1, UP2, UP3)
};

// Function to determine the priority level of the packet based on its UP (User Priority) and traffic category
PriorityLevel determinePacketPriority(BaselineMacPacket *pkt) {
    // Implement your logic here to determine the priority level of the packet
    // You can access the UP and traffic category information from the packet header
    // For simplicity, let's assume the UP value is stored in the variable 'userPriority'
    int userPriority = pkt->getUserPriority();
    if (userPriority == 7) {
        return PRIORITY_P1;
    } else if (userPriority >= 4 && userPriority <= 6) {
        return PRIORITY_P2;
    } else {
        return PRIORITY_P3;
    }
}

// Function to send a packet using CSMA/CA
void sendUsingCSMACA(BaselineMacPacket *pkt) {
    // Implement your CSMA/CA logic here to contend for the channel and send the packet
    // You can use timers, backoff algorithms, and listen-before-talk mechanisms
    // For simplicity, let's assume a basic CSMA/CA procedure:
    // 1. Perform clear channel assessment (CCA) to check if the channel is busy
    // 2. If the channel is busy, back off for a random period and retry
    // 3. If the channel is idle, send the packet

    if (isChannelIdle()) {
        // Channel is idle, send the packet
        toRadioLayer(pkt);
        toRadioLayer(createRadioCommand(SET_STATE, TX));
    } else {
        // Channel is busy, back off for a random period and retry
        double backoffTime = calculateRandomBackoff();
        setTimer(BACKOFF_TIMER, backoffTime);
    }
}

// Function to check if the channel is idle (no ongoing transmissions)
bool isChannelIdle() {
    // Implement your logic here to check if the channel is idle
    // This can be done by performing clear channel assessment (CCA)
    // If no ongoing transmissions are detected, return true, otherwise, return false.
    // For simplicity, let's assume the channel is always idle.
    return true;
}

// Function to calculate a random backoff time
double calculateRandomBackoff() {
    // Implement your logic here to calculate a random backoff time
    // This can be done using random number generators and the binary exponential backoff algorithm.
    // For simplicity, let's assume a fixed backoff time of 1 second.
    return 1.0;
}


void BaselineBANMac::handlePoll(BaselineMacPacket *pkt) {
    // check if this is an immediate (not future) poll
    if (pkt->getMoreData() == 0) {
        macState = MAC_FREE_TX_ACCESS;
        trace() << "State from " << macState << " to MAC_FREE_TX_ACCESS (poll)";
        isPollPeriod = true;
        int endPolledAccessSlot = pkt->getSequenceNumber();
        /* The end of the polled access time is given as the end of an allocation
         * slot. We have to know the start of the whole frame to calculate it.
         * NOTICE the difference in semantics with other end slots such as scheduled access
         * scheduledTxAccessEnd where the end is the beginning of scheduledTxAccessEnd
         * equals with the end of scheduledTxAccessEnd-1 slot.
         */
        endTime = frameStartTime + endPolledAccessSlot * allocationSlotLength;
        // reset the timer for sleeping as needed
        if (endPolledAccessSlot != beaconPeriodLength &&
            (endPolledAccessSlot + 1) != scheduledTxAccessStart && (endPolledAccessSlot + 1) != scheduledRxAccessStart) {
            setTimer(START_SLEEPING, endTime - getClock());
        } else {
            cancelTimer(START_SLEEPING);
        }

        int currentSlotEstimate = round(SIMTIME_DBL(getClock() - frameStartTime) / allocationSlotLength) + 1;
        if (currentSlotEstimate - 1 > beaconPeriodLength) {
            trace() << "WARNING: currentSlotEstimate= " << currentSlotEstimate;
        }
        collectOutput("var stats", "poll slots taken", (endPolledAccessSlot + 1) - currentSlotEstimate);
        attemptTX();
    }
    // else treat this as a POST: a post of the polling message coming in the future
    else {
        // seqNum holds the allocation slot that the post will happen and fragNum the num of beacon periods in the future
        int postedAccessStart = pkt->getSequenceNumber();
        postedAccessEnd = postedAccessStart + 1; // all posts last one slot, end here is the beginning of the end slot
        simtime_t postTime = frameStartTime + (postedAccessStart - 1 + pkt->getFragmentNumber() * beaconPeriodLength) * allocationSlotLength;
        trace() << "Future Poll received, postSlot= " << postedAccessStart << " waking up in " << postTime - GUARD_TIME - getClock();
        // if the post is the slot immediately after, then we have to check if we get a negative number for the timer
        if (postTime <= getClock() - GUARD_TIME) {
            setTimer(START_POSTED_ACCESS, 0);
        } else {
            setTimer(START_POSTED_ACCESS, postTime - GUARD_TIME - getClock());
        }
    }
}

void BaselineBANMac::handlePost(BaselineMacPacket *pkt) {
    if (isHub) {
        if (pollingEnabled) {
            // Handle moreData at the hub
            handleMoreDataAtHub(pkt);
        }
        // Can we make this a separate class HubDecisionLayer:: ?? Do we need too many variables from MAC?
        return;
    }

    // Find the current slot, this is the starting slot of the post
    int postedAccessStart = (int)round(SIMTIME_DBL(getClock() - frameStartTime) / allocationSlotLength) + 1;
    // Post lasts for the current slot. This can be problematic, since we might go to sleep
    // while receiving. We need a post timeout.
    postedAccessEnd = postedAccessStart + 1;
    setTimer(START_POSTED_ACCESS, 0);
}


void BaselineBANMac::handleMoreDataAtHub(BaselineMacPacket *pkt) {
    // Decide if this is the last packet that node NID can send, keep track of how much more data it has
    int NID = pkt->getNID();
    /* If the packet we received is in the node's last TX access slot (scheduled or polled) then send a POLL.
     * This means that we might send multiple polls (as we may receive multiple packets in the last slot),
     * but this is fine since all will point to the same time. Note that a node can only support one future
     * poll (one timer for START_POSTED_ACCESS). Sending multiple polls (especially with I_ACK+POLL which
     * do not cost anything extra compared to I_ACK) is beneficial because it increases the probability
     * of the poll's reception. Also, note that reqToSendMoreData[NID] will have the latest info (the info
     * carried by the last packet with moreData received). Finally, the lastTxAccessSlot[NID].polled does
     * not need to be reset for a new beacon period. If we send a new poll, this variable will be updated,
     * if we don't, then we will not receive packets from that NID in the old slot, so no harm done.
     */
    if (currentSlot == lastTxAccessSlot[NID].scheduled || currentSlot == lastTxAccessSlot[NID].polled) {
        if (nextFuturePollSlot <= beaconPeriodLength) {
            trace() << "Hub handles more Data (" << pkt->getMoreData() << ") from NID: " << NID << " current slot: " << currentSlot;
            reqToSendMoreData[NID] = pkt->getMoreData();
            // If an ack is required for the packet, the poll will be sent as an I_ACK_POLL
            if (pkt->getAckPolicy() == I_ACK_POLICY) {
                sendIAckPoll = true;
            } else {
                // Create a POLL message and send it. (Note: This part is not implemented in the given code snippet.)
                // Not implemented here since currently all the data packets require I_ACK
            }
        }
    }
}


void BaselineBANMac::timerFiredCallback(int index) {
    switch (index) {
        case CARRIER_SENSING: {
            // Specific logic for CARRIER_SENSING timer
            if (!canFitTx()) {
                attemptingToTX = false;
                currentPacketCSFails++;
                break;
            }
            CCAResult CCAcode = radioModule->isChannelClear();
            if (CCAcode == CLEAR) {
                backoffCounter--;
                if (backoffCounter > 0) setTimer(CARRIER_SENSING, contentionSlotLength);
                else {
                    sendUsingCSMACA();
                }
            } else {
                setTimer(CARRIER_SENSING, contentionSlotLength * 3.0);
            }
            break;
        }

        case START_ATTEMPT_TX: {
            futureAttemptToTX = false;
            attemptTX();
            break;
        }

        case ACK_TIMEOUT: {
            trace() << "ACK timeout fired";
            waitingForACK = false;

            // double the Contention Window, after every second fail.
            CWdouble ? CWdouble = false : CWdouble = true;
            if (CWdouble && CW < CWmax[priority]) CW *= 2;

            // check if we reached the max number and if so delete the packet
            if (currentPacketTransmissions + currentPacketCSFails == maxPacketTries) {
                if (packetToBeSent->getFrameType() == DATA) {
                    collectOutput("Data pkt breakdown", "Failed, No Ack");
                } else collectOutput("Mgmt & Ctrl pkt breakdown", "Failed, No Ack");
                cancelAndDelete(packetToBeSent);
                packetToBeSent = NULL;
                currentPacketTransmissions = 0;
                currentPacketCSFails = 0;
            }
            attemptTX();
            break;
        }

        case START_SLEEPING: {
            trace() << "State from " << macState << " to MAC_SLEEP";
            macState = MAC_SLEEP;
            toRadioLayer(createRadioCommand(SET_STATE, SLEEP));
            isRadioSleeping = true;
            isPollPeriod = false;
            break;
        }

        case START_SCHEDULED_TX_ACCESS: {
            trace() << "State from " << macState << " to MAC_FREE_TX_ACCESS (scheduled)";
            macState = MAC_FREE_TX_ACCESS;
            endTime = getClock() + (scheduledTxAccessEnd - scheduledTxAccessStart) * allocationSlotLength;
            if (beaconPeriodLength > scheduledTxAccessEnd)
                setTimer(START_SLEEPING, (scheduledTxAccessEnd - scheduledTxAccessStart) * allocationSlotLength);
            attemptTX();
            break;
        }

        case START_SCHEDULED_RX_ACCESS: {
            trace() << "State from " << macState << " to MAC_FREE_RX_ACCESS (scheduled)";
            macState = MAC_FREE_RX_ACCESS;
            toRadioLayer(createRadioCommand(SET_STATE, RX));
            isRadioSleeping = false;
            if (beaconPeriodLength > scheduledRxAccessEnd)
                setTimer(START_SLEEPING, (scheduledRxAccessEnd - scheduledRxAccessStart) * allocationSlotLength);
            break;
        }

        case START_POSTED_ACCESS: {
            trace() << "State from " << macState << " to MAC_FREE_RX_ACCESS (post)";
            macState = MAC_FREE_RX_ACCESS;
            toRadioLayer(createRadioCommand(SET_STATE, RX));
            isRadioSleeping = false;
            // reset the timer for sleeping as needed
            if ((postedAccessEnd - 1) != beaconPeriodLength &&
                postedAccessEnd != scheduledTxAccessStart && postedAccessEnd != scheduledRxAccessStart) {
                setTimer(START_SLEEPING, allocationSlotLength);
            } else cancelTimer(START_SLEEPING);
            break;
        }

        case WAKEUP_FOR_BEACON: {
            trace() << "State from " << macState << " to MAC_BEACON_WAIT";
            macState = MAC_BEACON_WAIT;
            toRadioLayer(createRadioCommand(SET_STATE, RX));
            isRadioSleeping = false;
            isPollPeriod = false;
            break;
        }

        case SYNC_INTERVAL_TIMEOUT: {
            pastSyncIntervalNominal = true;
            syncIntervalAdditionalStart = getClock();
            break;
        }

        case START_SETUP: {
            macState = MAC_SETUP;
            break;
        }

        case SEND_BEACON: {
    trace() << "BEACON SEND, next beacon in " << beaconPeriodLength * allocationSlotLength;
    trace() << "State from " << macState << " to MAC_RAP";
    macState = MAC_RAP;
    setTimer(SEND_BEACON, beaconPeriodLength * allocationSlotLength);
    setTimer(HUB_SCHEDULED_ACCESS, RAP1Length * allocationSlotLength);
    endTime = getClock() + RAP1Length * allocationSlotLength;

    // Creating and sending the beacon packet
    BaselineBeaconPacket *beaconPkt = new BaselineBeaconPacket("BaselineBAN beacon", MAC_LAYER_PACKET);
    setHeaderFields(beaconPkt, N_ACK_POLICY, MANAGEMENT, BEACON);
    beaconPkt->setNID(BROADCAST_NID);

    beaconPkt->setAllocationSlotLength((int)(allocationSlotLength * 1000));
    beaconPkt->setBeaconPeriodLength(beaconPeriodLength);
    beaconPkt->setRAP1Length(RAP1Length);
    beaconPkt->setByteLength(BASELINEBAN_BEACON_SIZE);

    toRadioLayer(beaconPkt);
    toRadioLayer(createRadioCommand(SET_STATE, TX));
    isRadioSleeping = false;

    // Read the long comment in sendPacket() to understand why we add 2*pTIFS
    setTimer(START_ATTEMPT_TX, (TX_TIME(beaconPkt->getByteLength()) + 2 * pTIFS));
    futureAttemptToTX = true;

    collectOutput("Beacons sent");
    // Keep track of the current slot and the frame start time
    frameStartTime = getClock();
    currentSlot = 1;
    setTimer(INCREMENT_SLOT, allocationSlotLength);
    // Free slots for polls happen after RAP and scheduled access
    nextFuturePollSlot = currentFirstFreeSlot;

    // If implementing a naive polling scheme, we will send a bunch of future polls in the first free slot for polls
    if (naivePollingScheme && pollingEnabled && nextFuturePollSlot <= beaconPeriodLength) {
        setTimer(SEND_FUTURE_POLLS, (nextFuturePollSlot - 1) * allocationSlotLength);
    }
    break;
}


        case SEND_FUTURE_POLLS: {
    trace() << "State from " << macState << " to MAC_FREE_TX_ACCESS (send Future Polls)";
    macState = MAC_FREE_TX_ACCESS;
    endTime = getClock() + allocationSlotLength;

    // The current slot is used to TX the future polls, so we have 1 less slot available
    int availableSlots = beaconPeriodLength - (currentSlot - 1) - 1;
    if (availableSlots <= 0) break;

    int totalRequests = 0;
    // Our (immediate) polls should start one slot after the current one.
    int nextPollStart = currentSlot + 1;
    for (int nid = 0; nid < 256; nid++) totalRequests += reqToSendMoreData[nid];
    if (totalRequests == 0) break;

    for (int nid = 0; nid < 256; nid++) {
        if (reqToSendMoreData[nid] > 0) {
            // A very simple assignment scheme. It can leave several slots unused
            int slotsGiven = floor(((float)reqToSendMoreData[nid] / (float)totalRequests) * availableSlots);
            if (slotsGiven == 0) continue;
            TimerInfo t;
            t.NID = nid;
            t.slotsGiven = slotsGiven;
            t.endSlot = nextPollStart + slotsGiven - 1;
            hubPollTimers.push(t);
            reqToSendMoreData[nid] = 0; // Reset the requested resources

            // Create the future POLL packet and buffer it
            BaselineMacPacket *pollPkt = new BaselineMacPacket("BaselineBAN Future Poll", MAC_LAYER_PACKET);
            setHeaderFields(pollPkt, N_ACK_POLICY, MANAGEMENT, POLL);
            pollPkt->setNID(nid);
            pollPkt->setSequenceNumber(nextPollStart);
            pollPkt->setFragmentNumber(0);
            pollPkt->setMoreData(1);
            pollPkt->setByteLength(BASELINEBAN_HEADER_SIZE);
            trace() << "Created future POLL for NID: " << nid << ", for slot " << nextPollStart;
            nextPollStart += slotsGiven;

            // Collect statistics or do other necessary actions
            // collectOutput("Polls given", nid);

            MgmtBuffer.push(pollPkt);
        }
    }

    // The first poll will be sent one slot after the current one.
    if (!hubPollTimers.empty()) setTimer(SEND_POLL, allocationSlotLength);
    // TX all the future POLL packets created
    attemptTX();
    break;
}

case SEND_POLL: {
    if (hubPollTimers.empty()) {
        trace() << "WARNING: timer SEND_POLL with hubPollTimers NULL";
        break;
    }
    trace() << "State from " << macState << " to MAC_FREE_RX_ACCESS (Poll)";
    macState = MAC_FREE_RX_ACCESS;

    // We set the state to RX but we also need to send the POLL message.
    TimerInfo t = hubPollTimers.front();
    int slotsGiven = t.slotsGiven;
    BaselineMacPacket *pollPkt = new BaselineMacPacket("BaselineBAN Immediate Poll", MAC_LAYER_PACKET);
    setHeaderFields(pollPkt, N_ACK_POLICY, MANAGEMENT, POLL);
    pollPkt->setNID(t.NID);
    pollPkt->setSequenceNumber(t.endSlot);
    pollPkt->setFragmentNumber(0);
    pollPkt->setMoreData(0);
    pollPkt->setByteLength(BASELINEBAN_HEADER_SIZE);

    toRadioLayer(pollPkt);
    toRadioLayer(createRadioCommand(SET_STATE, TX));
    isRadioSleeping = false;

    collectOutput("var stats", "poll slots given", t.slotsGiven);
    trace() << "POLL for NID: " << t.NID << ", ending at slot: " << t.endSlot << ", lasting: " << t.slotsGiven << " slots";
    hubPollTimers.pop();

    // If there is another poll, then it will come after this one, so scheduling the timer is easy
    if (hubPollTimers.size() > 0) setTimer(SEND_POLL, slotsGiven * allocationSlotLength);
    break;
}


        case INCREMENT_SLOT: {
            currentSlot++;
            if (currentSlot < beaconPeriodLength) setTimer(INCREMENT_SLOT, allocationSlotLength);
            break;
        }

        case HUB_SCHEDULED_ACCESS: {
    trace() << "State from " << macState << " to MAC_FREE_RX_ACCESS (hub)";
    macState = MAC_FREE_RX_ACCESS;

    // We should look at the schedule and set up timers to get in and out
    // of MAC_FREE_RX_ACCESS, MAC_FREE_TX_ACCESS, and finally MAC_SLEEP

    // Check if there are any scheduled TX or RX slots for the hub
    bool hasScheduledTX = false;
    bool hasScheduledRX = false;

    // Assume you have some data structures to store the schedule information
    // and check if the hub has any scheduled TX or RX slots based on its NID
    int hubNID = getHubNID(); // Replace this with the actual NID of the hub

    // Check for scheduled TX slots for the hub
    if (hasScheduledTXSlot(hubNID)) {
        hasScheduledTX = true;

        // Get the start and end slots of the scheduled TX access
        int scheduledTXStart = getScheduledTXStartSlot(hubNID);
        int scheduledTXEnd = getScheduledTXEndSlot(hubNID);

        // Set the timer for entering the scheduled TX access
        simtime_t scheduledTXStartTime = frameStartTime + (scheduledTXStart - 1) * allocationSlotLength;
        setTimer(START_SCHEDULED_TX_ACCESS, scheduledTXStartTime - getClock());

        // Set the timer for exiting the scheduled TX access
        simtime_t scheduledTXEndTime = frameStartTime + scheduledTXEnd * allocationSlotLength;
        setTimer(END_SCHEDULED_TX_ACCESS, scheduledTXEndTime - getClock());
    }

    // Check for scheduled RX slots for the hub
    if (hasScheduledRXSlot(hubNID)) {
        hasScheduledRX = true;

        // Get the start and end slots of the scheduled RX access
        int scheduledRXStart = getScheduledRXStartSlot(hubNID);
        int scheduledRXEnd = getScheduledRXEndSlot(hubNID);

        // Set the timer for entering the scheduled RX access
        simtime_t scheduledRXStartTime = frameStartTime + (scheduledRXStart - 1) * allocationSlotLength;
        setTimer(START_SCHEDULED_RX_ACCESS, scheduledRXStartTime - getClock());

        // Set the timer for exiting the scheduled RX access
        simtime_t scheduledRXEndTime = frameStartTime + scheduledRXEnd * allocationSlotLength;
        setTimer(END_SCHEDULED_RX_ACCESS, scheduledRXEndTime - getClock());
    }

    // If there are no scheduled slots, the hub should directly go to sleep
    if (!hasScheduledTX && !hasScheduledRX) {
        trace() << "State from " << macState << " to MAC_SLEEP";
        macState = MAC_SLEEP;
        toRadioLayer(createRadioCommand(SET_STATE, SLEEP));
        isRadioSleeping = true;
    }

    break;
}

    }
}



// Helper function to check if the hub has any scheduled TX slots
bool BaselineBANMac::hasScheduledTXSlot(int hubNID) {
    // Replace this with your actual implementation to check if the hub
    // with the given NID has any scheduled TX slots.
    // Return true if there are scheduled TX slots, false otherwise.
    // You can use your data structures to store and manage the schedule information.
    // Example:
    // return scheduleManager.hasScheduledTXSlots(hubNID);
    // where scheduleManager is an instance of your schedule management class.
    // Make sure to implement the corresponding methods in your schedule manager class.
    return false;
}

// Helper function to get the start slot of the scheduled TX access for the hub
int BaselineBANMac::getScheduledTXStartSlot(int hubNID) {
    // Replace this with your actual implementation to get the start slot
    // of the scheduled TX access for the hub with the given NID.
    // Return the start slot number.
    // You can use your data structures to store and manage the schedule information.
    // Example:
    // return scheduleManager.getScheduledTXStartSlot(hubNID);
    // where scheduleManager is an instance of your schedule management class.
    // Make sure to implement the corresponding methods in your schedule manager class.
    return 0;
}

// Helper function to get the end slot of the scheduled TX access for the hub
int BaselineBANMac::getScheduledTXEndSlot(int hubNID) {
    // Replace this with your actual implementation to get the end slot
    // of the scheduled TX access for the hub with the given NID.
    // Return the end slot number.
    // You can use your data structures to store and manage the schedule information.
    // Example:
    // return scheduleManager.getScheduledTXEndSlot(hubNID);
    // where scheduleManager is an instance of your schedule management class.
    // Make sure to implement the corresponding methods in your schedule manager class.
    return 0;
}

// Helper function to check if the hub has any scheduled RX slots
bool BaselineBANMac::hasScheduledRXSlot(int hubNID) {
    // Replace this with your actual implementation to check if the hub
    // with the given NID has any scheduled RX slots.
    // Return true if there are scheduled RX slots, false otherwise.
    // You can use your data structures to store and manage the schedule information.
    // Example:
    // return scheduleManager.hasScheduledRXSlots(hubNID);
    // where scheduleManager is an instance of your schedule management class.
    // Make sure to implement the corresponding methods in your schedule manager class.
    return false;
}

// Helper function to get the start slot of the scheduled RX access for the hub
int BaselineBANMac::getScheduledRXStartSlot(int hubNID) {
    // Replace this with your actual implementation to get the start slot
    // of the scheduled RX access for the hub with the given NID.
    // Return the start slot number.
    // You can use your data structures to store and manage the schedule information.
    // Example:
    // return scheduleManager.getScheduledRXStartSlot(hubNID);
    // where scheduleManager is an instance of your schedule management class.
    // Make sure to implement the corresponding methods in your schedule manager class.
    return 0;
}

// Helper function to get the end slot of the scheduled RX access for the hub
int BaselineBANMac::getScheduledRXEndSlot(int hubNID) {
    // Replace this with your actual implementation to get the end slot
    // of the scheduled RX access for the hub with the given NID.
    // Return the end slot number.
    // You can use your data structures to store and manage the schedule information.
    // Example:
    // return scheduleManager.getScheduledRXEndSlot(hubNID);
    // where scheduleManager is an instance of your schedule management class.
    // Make sure to implement the corresponding methods in your schedule manager class.
    return 0;
}

















void BaselineBANMac::fromRadioLayer(cPacket *pkt, double rssi, double lqi) {

   BaselineMacPacket * BaselineBANPkt = dynamic_cast<BaselineMacPacket*>(pkt);  
   if (BaselineBANPkt == NULL) return;
    
   int priority = BaselineBANPkt->getPriority();
   
   if (!isPacketForMe(BaselineBANPkt)) return;

   if (BaselineBANPkt->getFrameType() == DATA) {  
     toNetworkLayer(decapsulatePacket(BaselineBANPkt));   
   }

   if (BaselineBANPkt->getAckPolicy() == I_ACK_POLICY) {  
         
      BaselineMacPacket * ackPacket = new BaselineMacPacket("ACK packet",MAC_LAYER_PACKET);    
      
      if (priority == HIGH_PRIORITY) {
         
         ackPacket->setFrameSubtype(HIGH_PRIORITY);  
         ackPacket->setSequenceNumber(getNextSeqNumber() + HIGH_PRIORITY_OFFSET);
         ackPacket->setFRAG(ackPacket->getFRAG() + FRAG_OFFSET);

         toRadioLayer(ackPacket);     
         toRadioLayer(createRadioCommand(SET_STATE,TX));
         setTimer(START_ATTEMPT_TX, (TX_TIME(BASELINEBAN_HEADER_SIZE) + pTIFS) );
                 
        collectOutput("ACK latency", "Low");  
        
      }  
      else {
            
          ackPacket->setFrameSubtype(LOW_PRIORITY); 
          ackPacket->setSequenceNumber(getNextSeqNumber());    
          
          toRadioLayer(ackPacket);   
          toRadioLayer(createRadioCommand(SET_STATE,TX)); 
          setTimer(START_ATTEMPT_TX, (TX_TIME(BASELINEBAN_HEADER_SIZE) + 2*pTIFS) );
      
          collectOutput("ACK latency", "Default");       
      }       
        
   }
   /* If this was a data packet, we have done all our processing
	 * (+ sending a possible I-ACK or I-ACK-POLL), so just return.
	 */
	if (BaselineBANPkt->getFrameType() == DATA) return;

	/* Handle management and control packets */
	switch(BaselineBANPkt->getFrameSubtype()) {


}

void BaselineBANMac::finishSpecific(){

   int pktPriority = (packetToBeSent) ? packetToBeSent->getPriority() : 0;

   if(pktPriority == HIGH_PRIORITY){
        collectOutput("High priority pkts", "Dropped at finish");
   }   
   else {
        collectOutput("Medium/low priority pkts", "Dropped at finish");
   }

   if (packetToBeSent != NULL) cancelAndDelete(packetToBeSent);
   
   BaselineMacPacket *highPkt;
   
   while(!MgmtBuffer.empty()) {
       
      if (MgmtBuffer.front()->getPriority() == HIGH_PRIORITY) {
         highPkt = MgmtBuffer.front();    
      }     
      else {
          cancelAndDelete(MgmtBuffer.front());
          MgmtBuffer.pop();
      }
            
   }

   if (highPkt) {    
       cancelAndDelete(highPkt);
   }  
   
   ...
   
}


bool BaselineBANMac::isPacketForMe(BaselineMacPacket *pkt) {
    
    int priority = pkt->getPriority();
    
    if (priority == HIGH_PRIORITY) return true;
    
    int pktHID = pkt->getHID();
    int pktNID = pkt->getNID();
    
    ...
    
    if (connectedHID == pktHID) {
      
        ...
                
    }   
    else if ((connectedHID == UNCONNECTED) && ...) {
      
        ...
        
        if (priority == HIGH_PRIORITY) {
           collectOutput("Packet receiver", "High priority"); 
        }
        else {
           ...
        }
        
        if (pkt->getFrameSubtype() == CONNECTION_ASSIGNMENT) { 
            
           ...
               
        }   
        
        if (pkt->getFrameSubtype() == CONNECTION_REQUEST) return false;  
        
        ...
            
        if (priority == HIGH_PRIORITY) {
           unconnectedNID = genk_intrand(15, 30);   
        }    
        else {
           unconnectedNID = 1 + genk_intrand(0,14);    
        }  
        
    }
    
    ...
    
}


simtime_t BaselineBANMac::extraGuardTime() {
   
   int priority = packetToBeSent->getPriority();
   
   if (priority == HIGH_PRIORITY) {
      
       simtime_t guardTime = (getClock() - syncIntervalAdditionalStart) * SHORT_ACCURACY;
       
       collectOutput("Guard time", "Short");
       
       return guardTime;    
   }
   else {
       
       simtime_t guardTime = (getClock() - syncIntervalAdditionalStart) * mClockAccuracy;
       
       collectOutput("Guard time", "Default");
       
       return guardTime;
   }       
}


void BaselineBANMac::setHeaderFields(BaselineMacPacket * pkt, AcknowledgementPolicy_type ackPolicy, Frame_type frameType, Frame_subtype frameSubtype) {
   
    int priority = pkt->getPriority();
    
    pkt->setHID(connectedHID);
    
    if (priority == HIGH_PRIORITY) {
       pkt->setFrameSubtype(HIGH_PRIORITY);  
       pkt->setFRAG(pkt->getFRAG() + FRAG_OFFSET);
       collectOutput("packet priority", "High");   
    }
    else if (priority == MEDIUM_PRIORITY) {
       pkt->setFrameSubtype(MEDIUM_PRIORITY);
       collectOutput("packet priority", "Medium");    
    }   
    else {
       pkt->setFrameSubtype(LOW_PRIORITY);
       collectOutput("packet priority", "Low");       
    }  
    
    pkt->setAckPolicy(ackPolicy);
            
    if (frameType == DATA && !isHub){
        
    pkt->setMoreData(0);  
      
    if (TXBuffer.size()!=0 || MgmtBuffer.size()!=0){
         
         // Option to enhance BaselineBAN by sending how many more packets we have
         if (enhanceMoreData) {
             if (priority == HIGH_PRIORITY) {
                  pkt->setMoreData(TXBuffer.size() + MgmtBuffer.size() + HIGH_PRIORITY_OFFSET);   
             }
             else {
                  pkt->setMoreData(TXBuffer.size() + MgmtBuffer.size());   
             }  
         }  
         else {
            pkt->setMoreData(1);  
         }
        
    }
}
    
}



void BaselineBANMac::attemptTxInCAP(){
    
    int priority = packetToBeSent->getPriority();  
  
    if (backoffCounter == 0) {
     
        backoffCounter = genk_intrand(0, CW);
             
    }   
   
    trace() << "Starting to transmit " << packetToBeSent->getName()
        << " in CAP, backoffCounter " << backoffCounter;
  
    attemptingToTX = true;
    
    setTimer(CARRIER_SENSING, CARRIER_SENSE_TIME);
  
}


void BaselineBANMac::attemptTxInEAP(){
   
   int priority = packetToBeSent->getPriority();
   
   if (backoffCounter == 0) {
     
      if (priority == HIGH_PRIORITY) {
         backoffCounter = genk_intrand(0, SMALL_CW);  
      }
   }   
   
   trace()...
   
   attemptingToTX = true;
      
   if (priority == HIGH_PRIORITY) {
      setTimer(CARRIER_SENSING, SHORT_TIME);  
   }

}


void BaselineBANMac::attemptTxInRAP() {
   
   int priority = packetToBeSent->getPriority();
   
   if (backoffCounter == 0) {
     
      if (priority == HIGH_PRIORITY) {
         backoffCounter = genk_intrand(0, SMALL_CW);  
      }
      else {
         backoffCounter = genk_intrand(0, CW);  
      }
   }   
   
   trace()
   
   attemptingToTX = true;
      
   if (priority == HIGH_PRIORITY) {
      setTimer(CARRIER_SENSING, SHORT_TIME);
   }
   else {
      setTimer(CARRIER_SENSING, CARRIER_SENSE_TIME); 
   }
   
}




void BaselineBANMac::attemptTX() {
  
    int priority = packetToBeSent->getPriority();  
   
    if (priority == HIGH_PRIORITY && 
        (macState == MAC_FREE_TX_ACCESS || currentPhase == EAP)){
        
        sendPacket();
        return;  
    }
      
    if(priority == HIGH_PRIORITY){
        maxPacketTries += HIGH_PRIORITY_OFFSET;
    }
    else {
        maxPacketTries = DEFAULT_MAX_PACKET_TRIES; 
    }
   
    // Rest of function body as before...   
   
    if (packetToBeSent && currentPacketTransmissions + currentPacketCSFails < maxPacketTries) {
       
        
       
        if (macState == MAC_FREE_TX_ACCESS && canFitTx()){
          
            sendPacket();  
            return;
        }

        if (currentPhase == EAP && canFitTx()){
               attemptTxInEAP();   
           }
           else if (currentPhase == CAP && canFitTx()){    
               attemptTxInCAP();     
           }     
    }  
      
    if (packetToBeSent) {   
      
        if (priority == HIGH_PRIORITY){
            collectOutput("pkt breakdown", "High priority failed");   
        }
        else {
            collectOutput("pkt breakdown", "Medium/low priority failed");     
        }
              
        cancelAndDelete(packetToBeSent);
		packetToBeSent = NULL;
		currentPacketTransmissions = 0;
		currentPacketCSFails = 0;
      
   }      
      
}


bool BaselineBANMac::canFitTx() {
  
    int priority = packetToBeSent->getPriority();
   
    if (priority == HIGH_PRIORITY && currentPhase == EAP) {
        simtime_t guardTime = SHORT_GUARD_TIME;
        if( endTime - getClock() - guardTime - TX_TIME(packetToBeSent->getByteLength()) - pTIFS 
            > availableTimeInEAP ){
            return true;
        }
    } 
    else {
        simtime_t guardTime = GUARD_TIME;  
        if( endTime - getClock() - guardTime - TX_TIME(packetToBeSent->getByteLength()) - pTIFS
            > availableTimeInCurrentPhase ){
            return true;
        }
    }
   
    return false;
   
}



void BaselineBANMac::sendPacket() {
  
    int priority = packetToBeSent->getPriority();
    
    // Exit attemptingToTX state   
    attemptingToTX = false;
    
    // Collect stats based on priority
    // Collect stats based on priority
    if (priority == HIGH_PRIORITY) {
        collectOutput("pkt TX state breakdown", "High priority");   
    }
    else if (priority == MEDIUM_PRIORITY) {
        collectOutput("pkt TX state breakdown", "Medium priority");  
    }    
    else {
        collectOutput("pkt TX state breakdown", "Low priority");
    }
    
    if (packetToBeSent->getAckPolicy() == I_ACK_POLICY || packetToBeSent->getB_ACK_POLICY) {   
       
        if (priority == HIGH_PRIORITY) {
            setTimer(ACK_TIMEOUT, SHORT_TIMEOUT);  
        }  
        else {
            setTimer(ACK_TIMEOUT, LONG_TIMEOUT);
        }   
         
         
        toRadioLayer(packetToBeSent->dup());  
        toRadioLayer(createRadioCommand(SET_STATE,TX));
         
    } 
    else {  
      
        if (priority == HIGH_PRIORITY) {
            setTimer(START_ATTEMPT_TX, SHORT_WAIT);  
        }
        else {
            setTimer(START_ATTEMPT_TX, LONG_WAIT); 
        } 
    }
    
}



void BaselineBANMac::handlePoll(BaselineMacPacket *pkt) {
    int priority = pkt->getPriority();
    
    if (pkt->getMoreData() == 0){  // Immediate poll 
        
        if(priority == HIGH_PRIORITY && currentPhase == EAP){   
   
            // Check if there are available slots in current EAP phase
            if( availableTimeslotsInEAP > 0){

                // Schedule transmission for polled node in next EAP slot
                scheduleTransmission(pkt->getSrcAddress(), currentEAPSlot);
                
                // Decrease available EAP slots
                availableTimeslotsInEAP--;  
                
                // Set node state to transmit  
                macState = MAC_FREE_TX_ACCESS; 
                
                // Attempt TX immediately  
                attemptTX();  
            }
            else{
                // No EAP slots available, send NACK    
                generateNACK(pkt);    
            }
        }
        else {    

            // Check if there are available slots in current RAP/CAP phase
            if( availableTimeslotsInCurrentPhase > 0){

                // Schedule transmission for polled node in next available slot
                scheduleTransmission(pkt->getSrcAddress(), currentAvailableSlot); 
                
                // Decrease available slots
                availableTimeslotsInCurrentPhase--;
                
                // Set node state to transmit  
                macState = MAC_FREE_TX_ACCESS; 
                ...
                
                // Attempt TX immediately
                attemptTX();
            }
            else{
                // No slots available, send NACK
                generateNACK(pkt);   
            }   
        
            int endPolledAccessSlot = pkt->getSequenceNumber();
            /* The end of the polled access time is given as the end of an allocation
            * slot. We have to know the start of the whole frame to calculate it.
            * NOTICE the difference in semantics with other end slots such as scheduled access
            * scheduledTxAccessEnd where the end is the beginning of scheduledTxAccessEnd
            * equals with the end of scheduledTxAccessEnd-1 slot.
            */
            endTime = frameStartTime + endPolledAccessSlot * allocationSlotLength;
            // reset the timer for sleeping as needed
            if (endPolledAccessSlot != beaconPeriodLength &&
            (endPolledAccessSlot+1) != scheduledTxAccessStart && (endPolledAccessSlot+1) != scheduledRxAccessStart){
                setTimer(START_SLEEPING, endTime - getClock());
            }else cancelTimer(START_SLEEPING);

            int currentSlotEstimate = round(SIMTIME_DBL(getClock()-frameStartTime)/allocationSlotLength)+1;
            if (currentSlotEstimate-1 > beaconPeriodLength) trace() << "WARNING: currentSlotEstimate= "<< currentSlotEstimate;
            collectOutput("var stats", "poll slots taken", (endPolledAccessSlot+1) - currentSlotEstimate );
            attemptTX();
        } 
    else {  // Future poll
       if(priority == HIGH_PRIORITY){
           
           int eapSlot = calculateNextEAPSlot();
           
           postedAccessStart = eapSlot;  
           postedAccessEnd = eapSlot + 1;  
        
           setTimer(START_EAP_POSTED_ACCESS, pkt->getPollTime() - getClock());
      
       }
       else {
           
           int rapOrCapSlot = calculateNextRAPOrCAPSlot();  
           postedAccessStart = rapOrCapSlot;
           postedAccessEnd = rapOrCapSlot + 1;
           
           setTimer(START_RAP_OR_CAP_POSTED_ACCESS, pkt->getPollTime() - getClock());
       }       
   }
}

void BaselineBANMac::handlePost(BaselineMacPacket *pkt) {
    if (isHub) {
        if (pollingEnabled) handleMoreDataAtHub(pkt);
        return;  
    }

    int priority = pkt->getPriority();
   
    // Calculate start and end slots based on priority
    if (priority == HIGH_PRIORITY){
        postedAccessStart = calculateEAPStartSlot();
        postedAccessEnd = calculateEAPEndSlot();      
    }    
    else { 
        postedAccessStart = calculateRAPOrCAPStartSlot();
        postedAccessEnd = calculateRAPOrCAPEndSlot();
    }
   
    // Set timer based on priority  
    if (priority == HIGH_PRIORITY){
        setTimer(START_EAP_POSTED_ACCESS, 0);
    }
    else {
        setTimer(START_RAP_OR_CAP_POSTED_ACCESS, 0);
    } 
}

void BaselineBANMac::handleMoreDataAtHub(BaselineMacPacket *pkt) {
   int NID = pkt->getNID();  
   int priority = pkt->getPriority();
   
   // Check if packet is high priority and in EAP phase
   if (priority == HIGH_PRIORITY && currentPhase == EAP) {
        // Check if we have slots available in the current EAP phase
        if( availableTimeslotsInEAP > 0){
            // Schedule next transmission for this node in the EAP phase
            scheduleTransmission(NID, currentEAPSlot);
        
            // Decrease available timeslots
            availableTimeslotsInEAP--;  
        
            // Generate ACK 
            generateACK(pkt);
        
            // Do not generate poll for high priority packet 
            sendIAckPoll = false;        
        }    
        else {
            // No slots available, store packet in queue
            storePacketInPriorityQueue(pkt, priority);
        }       
    } else {
        // Check if there are available slots in current RAP or CAP phase  
        if( availableTimeslotsInCurrentPhase > 0){
            // Schedule next transmission for this node 
            scheduleTransmission(NID, currentAvailableSlot);
       
            // Decrease available slots
            availableTimeslotsInCurrentPhase--;  
       
            // Generate ACK  
            generateACK(pkt);
       
            // Check if node has more data, generate poll if so
            if(pkt->getMoreData()) {
                sendIAckPoll = true;  
            }
        }
        else {
            // No slots available, store packet in priority queue
            storePacketInPriorityQueue(pkt, priority);  
        }
    }
   
    if (currentSlot == lastTxAccessSlot[NID].scheduled || currentSlot == lastTxAccessSlot[NID].polled){
		if (nextFuturePollSlot <= beaconPeriodLength) {
			trace() << "Hub handles more Data ("<< pkt->getMoreData() <<")from NID: "<< NID <<" current slot: " << currentSlot;
			reqToSendMoreData[NID] = pkt->getMoreData();
			// if an ack is required for the packet the poll will be sent as an I_ACK_POLL
			if (pkt->getAckPolicy() == I_ACK_POLICY) sendIAckPoll = true;
			else {	// create a POLL message and send it.
					// Not implemeted here since currently all the data packets require I_ACK
			}
		}
	}
}



// added the three different phases
simtime_t BaselineBANMac::timeToNextBeacon(simtime_t interval, int index, int phase) {
    switch(phase) {
        case EAP: 
            return interval; // For high priority traffic
        case RAP:  
            return interval; // For medium priority traffic
        case CAP:
            return interval; // For low priority traffic
    } 
    return interval;
}




void scheduleTransmission(int NID, int slot){
    // Add node NID to schedule list for given slot
    scheduledNodes[slot].push_back(NID);
}

void generateACK(BaselineMacPacket* pkt){    
    // Generate ACK packet
    BaselineMacPacket* ackPkt = new BaselineMacPacket();
    ackPkt->setType(ACK);
    
    // Set destination and other fields    
    ackPkt->setDestAddress(pkt->getSrcAddress());
    // ...
    
    // Send ACK packet
    sendDown(ackPkt);
}

void storePacketInPriorityQueue(BaselineMacPacket* pkt, int priority){
    switch(priority){
        case HIGH_PRIORITY:
            highPriorityQueue.enqueue(pkt);
            break;
        case MEDIUM_PRIORITY: 
            mediumPriorityQueue.enqueue(pkt);
            break;  
        case LOW_PRIORITY:
            lowPriorityQueue.enqueue(pkt);      
            break;
    }   
}

int calculateEAPStartSlot(){
   int startSlot = (int)round(SIMTIME_DBL(getClock() - frameStartTime)/allocationSlotLength)+1;    
   while(scheduledNodes[startSlot].size() > 0){
       startSlot++;    
   }
   return startSlot;  
}

int calculateEAPEndSlot(){
   return calculateEAPStartSlot() + 1;  
}

int calculateRAPOrCAPStartSlot(){
   int startSlot = (int)round(SIMTIME_DBL(getClock() - currentPhaseStartTime)/allocationSlotLength)+1;
   return startSlot;    
}

int calculateRAPOrCAPEndSlot(){   
   return calculateRAPOrCAPStartSlot() + 1;   
}

int calculateNextEAPSlot(){
  
   for(int i = currentEAPSlot + 1; i < endEAPSlot; i++){
      if(scheduledNodes[i].size() == 0){
         return i;
      }
   }
   return -1; // No available slots 
}

int calculateNextRAPOrCAPSlot(){

    for(int i = currentRAPOrCAPSlot + 1; i < endRAPOrCAPSlot; i++){
        if(scheduledNodes[i].size() == 0){
            return i;
        }    
    }
    return -1; // No available slots
}

void generateNACK(BaselineMacPacket *pkt){
  
  BaselineMacPacket* nackPkt = new BaselineMacPacket();
  nackPkt->setType(NACK);
  
  // Set destination, source and other fields  
  nackPkt->setDestAddress(pkt->getSrcAddress());    
  
  // Send NACK packet
  sendDown(nackPkt);
}

void trace(char *str) {
   emit(lowerStratumOutputGate, str);
}