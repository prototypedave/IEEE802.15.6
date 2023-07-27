void BaselineBANMac::startup() {
	isHub = par("isHub");
	if (isHub) {
		connectedHID = SELF_MAC_ADDRESS % (2<<16); // keep the 16 LS bits as a short address
		connectedNID = BROADCAST_NID; // default value, usually overwritten
		currentFreeConnectedNID = 16; // start assigning connected NID from ID 16
		allocationSlotLength = (double) par("allocationSlotLength")/1000.0; // convert msec to sec
		beaconPeriodLength = par("beaconPeriodLength");
		RAP1Length = par("RAP1Length");
		currentFirstFreeSlot = RAP1Length + 1;
		setTimer(SEND_BEACON, 0);
		lastTxAccessSlot = new AccessSlot[216];
		reqToSendMoreData = new int[216];
		for (int i = 0; i < 216; i++) {
			lastTxAccessSlot[i].scheduled = 0;
			lastTxAccessSlot[i].polled = 0;
			reqToSendMoreData[i] = 0;
		}

		// new variables for EAP and CAP phases
		eapSlotLength = (double) par("eapSlotLength")/1000.0;
		capSlotLength = (double) par("capSlotLength")/1000.0;
		numEapSlots = par("numEapSlots");
		numCapSlots = par("numCapSlots");
		currentPhase = EAP_PHASE; // start with EAP phase

		// modify existing variables to account for EAP and CAP packets
		contentionSlotLength = (double) par("contentionSlotLength")/1000.0; // convert msec to sec;
		maxPacketTries = par("maxPacketTries");
		CW = CWmin[priority];
		CWdouble = false;
		backoffCounter = 0;

		packetToBeSent = NULL;
		currentPacketTransmissions = 0;
		currentPacketCSFails = 0;
		waitingForACK = false;
		futureAttemptToTX = false;
		attemptingToTX = false;
		isPollPeriod = false;

		// initialize phase-specific variables
		currentEapSlot = 0;
		currentCapSlot = 0;
		numPacketsInEapPhase = 0;
		numPacketsInCapPhase = 0;

	} else {
		connectedHID = UNCONNECTED;
		connectedNID = UNCONNECTED;
		unconnectedNID = 1 + genk_intrand(0,14);    //we select random unconnected NID
		trace() << "Selected random unconnected NID " << unconnectedNID;
		scheduledAccessLength = par("scheduledAccessLength");
		scheduledAccessPeriod = par("scheduledAccessPeriod");
		pastSyncIntervalNominal = false;
		macState = MAC_SETUP;
		SInominal = -1;
	}

	pTIFS = (double) par("pTIFS")/1000.0;
	pTimeSleepToTX = (double) par("pTimeSleepToTX")/1000.0;
	isRadioSleeping = false;
	phyLayerOverhead = par("phyLayerOverhead");
	phyDataRate = par("phyDataRate");
	priority = getParentModule()->getParentModule()->getSubmodule("Application")->par("priority");
	mClockAccuracy = par("mClockAccuracy");
	enhanceGuardTime = par("enhanceGuardTime");
	enhanceMoreData = par("enhanceMoreData");
	pollingEnabled = par("pollingEnabled");
	naivePollingScheme = par("naivePollingScheme");
	enableRAP = par("enableRAP");
	sendIAckPoll = false;	// only used by Hub, butmust be initialized for all
	currentSlot = -1;		// only used by Hub
	nextFuturePollSlot = -1;	// only used by Hub

	// initialize variables for EAP and CAP phases
	numPacketsInEapPhase = 0;
	numPacketsInCapPhase = 0;

	// declare output statistics
	declareOutput("Data pkt breakdown");
	declareOutput("Mgmt & Ctrl pkt breakdown");
	declareOutput("pkt TX state breakdown");
	declareOutput("Beacons received");
	declareOutput("Beacons sent");
	declareOutput("var stats");
}

void BaselineBANMac::timerFiredCallback(int index) {
	switch (index) {
        case TX_ATTEMPT: {
            if (!canFitTx()) {
                attemptingToTX = false;
                currentPacketCSFails++;
                break;
            }

            if (currentPhase == EAP_PHASE) {
                if (numPacketsInEapPhase == 0) {
                    currentPhase = CAP_PHASE;
                    break;
                }
            } else if (currentPhase == CAP_PHASE) {
                if (numPacketsInCapPhase == 0) {
                    currentPhase = EAP_PHASE;
                    break;
                }
            }

            sendPacket();

            if (!attemptingToTX) {
                backoffCounter = 0;
                setTimer(CARRIER_SENSING, contentionSlotLength);
                break;
            }
            backoffCounter--;
            if (backoffCounter > 0) {
                setTimer(TX_ATTEMPT, contentionSlotLength);
            } else {
                backoffCounter = computeBackoffCounter();
                currentPacketRetries++;
                setTimer(CARRIER_SENSING, scheduledTxAccessEnd - simTime().dbl());
            }
            break;
        }

        case ACK_TIMEOUT: {
            trace() << "ACK timeout fired";
            waitingForACK = false;

            // double the Contention Window, after every second fail.
            CWdouble ? CWdouble=false : CWdouble=true;
            if ((CWdouble) && (CW < CWmax[priority])) CW *=2;

            // check if we reached the max number and if so delete the packet
            if (currentPacketTransmissions + currentPacketCSFails == maxPacketTries) {
                // collect statistics
                if (packetToBeSent->getFrameType() == DATA) {
                    collectOutput("Data pkt breakdown", "Failed, No Ack");
                } else collectOutput("Mgmt & Ctrl pkt breakdown", "Failed, No Ack");
                cancelAndDelete(packetToBeSent);
                packetToBeSent = NULL;
                currentPacketTransmissions = 0;
                currentPacketCSFails = 0;
                if (currentPhase == EAP_PHASE) {
                    numPacketsInEapPhase--;
                } else if (currentPhase == CAP_PHASE) {
                    numPacketsInCapPhase--;
                }
                if (numPacketsInEapPhase == 0 && numPacketsInCapPhase == 0) {
                    currentPhase = EAP_PHASE;
                    break;
                }
            }

            if (currentPhase == EAP_PHASE) {
                if (numPacketsInEapPhase == 0) {
                    currentPhase = CAP_PHASE;
                    break;
                }
            } else if (currentPhase == CAP_PHASE) {
                if (numPacketsInCapPhase == 0) {
                    currentPhase = EAP_PHASE;
                    break;
                }
            }

            attemptTX();
            break;
        }

        case START_SLEEPING: {
            trace() << "State from "<< macState << " to MAC_SLEEP";
            macState = MAC_SLEEP;
            toRadioLayer(createRadioCommand(SET_STATE,SLEEP));   isRadioSleeping = true;
            isPollPeriod = false;

            if (currentPhase == EAP_PHASE) {
                numPacketsInEapPhase = numPacketsToSendEapPhase;
                currentPacketRetries = 0;
                currentPacketCSFails = 0;
                currentPacketTransmissions = 0;
                currentPhase = CAP_PHASE;
                setTimer(TX_ATTEMPT, 0);
            } else if (currentPhase == CAP_PHASE) {
                numPacketsInCapPhase = numPacketsToSendCapPhase;
                currentPacketRetries = 0;
                currentPacketCSFails = 0;
                currentPacketTransmissions = 0;
                currentPhase = EAP_PHASE;
                setTimer(TX_ATTEMPT, 0);
            }

            break;
        }

        case START_SCHEDULED_TX_ACCESS: {
            trace() << "State from "<< macState << " to MAC_FREE_TX_ACCESS (scheduled)";
            macState = MAC_FREE_TX_ACCESS;
            endTime = getClock() + (scheduledTxAccessEnd - scheduledTxAccessStart) * allocationSlotLength;
            if (beaconPeriodLength > scheduledTxAccessEnd) {
                setTimer(START_SLEEPING, (scheduledTxAccessEnd - scheduledTxAccessStart) * allocationSlotLength);
            }

            if (currentPhase == EAP_PHASE) {
                if (numPacketsInEapPhase == 0) {
                    currentPhase = CAP_PHASE;
                    break;
                }
            } else if (currentPhase == CAP_PHASE) {
                if (numPacketsInCapPhase == 0) {
                    currentPhase = EAP_PHASE;
                    break;
                }
            }

            attemptTX();
            break;
        }
 
        // unchanged
        case START_SCHEDULED_RX_ACCESS: {
			trace() << "State from "<< macState << " to MAC_FREE_RX_ACCESS (scheduled)";
			macState = MAC_FREE_RX_ACCESS;
			toRadioLayer(createRadioCommand(SET_STATE,RX));  isRadioSleeping = false;
			if (beaconPeriodLength > scheduledRxAccessEnd)
				setTimer(START_SLEEPING, (scheduledRxAccessEnd - scheduledRxAccessStart) * allocationSlotLength);
			break;
		}

        // unchanged
        case START_POSTED_ACCESS: {
			trace() << "State from "<< macState << " to MAC_FREE_RX_ACCESS (post)";
			macState = MAC_FREE_RX_ACCESS;
			toRadioLayer(createRadioCommand(SET_STATE,RX));  isRadioSleeping = false;
			// reset the timer for sleeping as needed
			if ((postedAccessEnd-1) != beaconPeriodLength &&
				postedAccessEnd != scheduledTxAccessStart && postedAccessEnd != scheduledRxAccessStart){
				// we could set the timer with the following ways:
				//setTimer(START_SLEEPING, frameStartTime + ((postedAccessEnd-1) * allocationSlotLength) - getClock());
				//setTimer(START_SLEEPING, (postedAccessEnd-postedAccessStart)* allocationSlotLength);
				// but this is simpler, since the duration is always 1 slot
				setTimer(START_SLEEPING, allocationSlotLength);
			}else cancelTimer(START_SLEEPING);
			break;
		}

        // unchanged
        case WAKEUP_FOR_BEACON: {
			trace() << "State from "<< macState << " to MAC_BEACON_WAIT";
			macState = MAC_BEACON_WAIT;
			toRadioLayer(createRadioCommand(SET_STATE,RX));  isRadioSleeping = false;
			isPollPeriod = false;
			break;
		}

        case START_SETUP: {
			macState = MAC_SETUP;
			break;
		}

        // The rest of the timers are specific to a Hub
		case SEND_BEACON: {
			trace() << "BEACON SEND, next beacon in " << beaconPeriodLength * allocationSlotLength;
			trace() << "State from "<< macState << " to MAC_RAP";
			macState = MAC_RAP;
			// We should provide for the case of the Hub sleeping. Here we ASSUME it is always ON!
			setTimer(SEND_BEACON, beaconPeriodLength * allocationSlotLength);
			setTimer(HUB_SCHEDULED_ACCESS, RAP1Length * allocationSlotLength);
			// the hub has to set its own endTime
			endTime = getClock() + RAP1Length * allocationSlotLength;

			BaselineBeaconPacket * beaconPkt = new BaselineBeaconPacket("BaselineBAN beacon",MAC_LAYER_PACKET);
			setHeaderFields(beaconPkt,N_ACK_POLICY,MANAGEMENT,BEACON);
			beaconPkt->setNID(BROADCAST_NID);

			beaconPkt->setAllocationSlotLength((int)(allocationSlotLength*1000));
			beaconPkt->setBeaconPeriodLength(beaconPeriodLength);
			beaconPkt->setRAP1Length(RAP1Length);
			beaconPkt->setByteLength(BASELINEBAN_BEACON_SIZE);

			toRadioLayer(beaconPkt);
			toRadioLayer(createRadioCommand(SET_STATE,TX));  isRadioSleeping = false;

			// read the long comment in sendPacket() to understand why we add 2*pTIFS
			setTimer(START_ATTEMPT_TX, (TX_TIME(beaconPkt->getByteLength()) + 2*pTIFS));
			futureAttemptToTX = true;

			collectOutput("Beacons sent");
			// keep track of the current slot and the frame start time
			frameStartTime = getClock();
			currentSlot = 1;
			setTimer(INCREMENT_SLOT, allocationSlotLength);
			// free slots for polls happen after RAP and scheduled access
			nextFuturePollSlot = currentFirstFreeSlot;
			// if implementing a naive polling scheme, we will send a bunch of future polls in the fist free slot for polls
			if (naivePollingScheme && pollingEnabled && nextFuturePollSlot <= beaconPeriodLength)
				setTimer(SEND_FUTURE_POLLS, (nextFuturePollSlot-1) * allocationSlotLength);
			break;
		}

        case SEND_FUTURE_POLLS: {
            trace() << "State from "<< macState << " to MAC_FREE_TX_ACCESS (send Future Polls)";
            macState = MAC_FREE_TX_ACCESS;
            // when we are in a state that we can TX, we should *always* set endTime
            endTime = getClock() + allocationSlotLength;

            // Determine the traffic priority of each sensor node based on the number of data requests
            int numHighPriorityNodes = 0;
            int numMediumPriorityNodes = 0;
            int numLowPriorityNodes = 0;
            for(int nid=0; nid<256; nid++){
                int numRequests = reqToSendMoreData[nid];
                if (numRequests > 0) {
                    if (numRequests >= 3) {
                        // High priority node
                        numHighPriorityNodes++;
                    } else if (numRequests == 2) {
                        // Medium priority node
                        numMediumPriorityNodes++;
                    } else {
                        // Low priority node
                        numLowPriorityNodes++;
                    }
                    // Reset the requested resources
                    reqToSendMoreData[nid] = 0;
                }
            }

            // Calculate the number of slots to allocate for each traffic priority level
            int totalAvailableSlots = beaconPeriodLength - (currentSlot - 1) - 1;
            int numHighPrioritySlots = ceil((float)numHighPriorityNodes / 5 * totalAvailableSlots);
            int numMediumPrioritySlots = ceil((float)numMediumPriorityNodes / 5 * totalAvailableSlots);
            int numLowPrioritySlots = totalAvailableSlots - numHighPrioritySlots - numMediumPrioritySlots;

            // Create and buffer the future poll packets for each traffic priority level
            int nextPollStart = currentSlot + 1;
            for(int i = 1; i <= 3; i++) {
                int numSlotsToAllocate = 0;
                if (i == 1) {
                    // High priority
                    numSlotsToAllocate = numHighPrioritySlots;
                } else if (i == 2) {
                    // Medium priority
                    numSlotsToAllocate = numMediumPrioritySlots;
                } else {
                    // Low priority
                    numSlotsToAllocate = numLowPrioritySlots;
                }

                int numNodesToAllocate = i == 1 ? numHighPriorityNodes : (i == 2 ? numMediumPriorityNodes : numLowPriorityNodes);
                int numSlotsAllocated = 0;

                for(int nid=0; nid<256; nid++){
                    int numRequests = reqToSendMoreData[nid];
                    if (numRequests > 0) {
                        int numSlotsForNode = ceil((float)numRequests / numNodesToAllocate * numSlotsToAllocate);
                        if (numSlotsForNode > 0) {
                            // Create the future poll packet for the node and buffer it
                            BaselineMacPacket *pollPkt = new BaselineMacPacket("BaselineBAN Future Poll", MAC_LAYER_PACKET);
                            setHeaderFields(pollPkt,N_ACK_POLICY,MANAGEMENT,POLL);
                            pollPkt->setNID(nid);
                            pollPkt->setSequenceNumber(nextPollStart);
                            pollPkt->setFragmentNumber(0);
                            pollPkt->setMoreData(1);
                            pollPkt->setByteLength(BASELINEBAN_HEADER_SIZE);
                            trace() << "Created future POLL for NID:" << nid << ", for slot "<< nextPollStart;

                            // Calculate the end slot for the node's time slot allocation
                            int endSlot = nextPollStart + numSlotsForNode - 1;
                            if (endSlot > currentSlot + beaconPeriodLength - 1) {
                                endSlot = currentSlot + beaconPeriodLength - 1;
                            }

                            // Schedule the transmission of the future poll packet and update the number of slots allocated
                            schedulePacketTransmission(pollPkt, nextPollStart, endSlot);
                            numSlotsAllocated += numSlotsForNode;
                        }
                    }
                }

                // Update the number of slots available for the next traffic priority level
                numSlotsToAllocate -= numSlotsAllocated;
            }

            break;
        }