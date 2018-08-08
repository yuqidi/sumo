/****************************************************************************/
// Eclipse SUMO, Simulation of Urban MObility; see https://eclipse.org/sumo
// Copyright (C) 2001-2018 German Aerospace Center (DLR) and others.
// This program and the accompanying materials
// are made available under the terms of the Eclipse Public License v2.0
// which accompanies this distribution, and is available at
// http://www.eclipse.org/legal/epl-v20.html
// SPDX-License-Identifier: EPL-2.0
/****************************************************************************/
/// @file    TraCIServerAPI_Person.cpp
/// @author  Daniel Krajzewicz
/// @date    26.05.2014
/// @version $Id$
///
// APIs for getting/setting person values via TraCI
/****************************************************************************/


// ===========================================================================
// included modules
// ===========================================================================
#include <config.h>

#include <utils/common/StringTokenizer.h>
#include <microsim/MSTransportableControl.h>
#include <microsim/MSVehicleControl.h>
#include <microsim/pedestrians/MSPerson.h>
#include <microsim/MSNet.h>
#include <microsim/MSEdge.h>
#include <libsumo/Person.h>
#include <libsumo/VehicleType.h>
#include "TraCIConstants.h"
#include "TraCIServer.h"
#include "TraCIServerAPI_VehicleType.h"
#include "TraCIServerAPI_Person.h"


// ===========================================================================
// method definitions
// ===========================================================================
bool
TraCIServerAPI_Person::processGet(TraCIServer& server, tcpip::Storage& inputStorage,
                                  tcpip::Storage& outputStorage) {
    const int variable = inputStorage.readUnsignedByte();
    const std::string id = inputStorage.readString();
    server.initWrapper(RESPONSE_GET_PERSON_VARIABLE, variable, id);
    try {
        if (!libsumo::Person::handleVariable(id, variable, &server) &&
            !libsumo::VehicleType::handleVariable(libsumo::Person::getTypeID(id), variable, &server)) {
            switch (variable) {
                case VAR_EDGES: {
                    int nextStageIndex = 0;
                    if (!server.readTypeCheckingInt(inputStorage, nextStageIndex)) {
                        return server.writeErrorStatusCmd(CMD_GET_PERSON_VARIABLE, "The message must contain the stage index.", outputStorage);
                    }
                    server.getWrapperStorage().writeUnsignedByte(TYPE_STRINGLIST);
                    server.getWrapperStorage().writeStringList(libsumo::Person::getEdges(id, nextStageIndex));
                    break;
                }
                case VAR_STAGE: {
                    int nextStageIndex = 0;
                    if (!server.readTypeCheckingInt(inputStorage, nextStageIndex)) {
                        return server.writeErrorStatusCmd(CMD_GET_PERSON_VARIABLE, "The message must contain the stage index.", outputStorage);
                    }
                    server.getWrapperStorage().writeUnsignedByte(TYPE_INTEGER);
                    server.getWrapperStorage().writeInt(libsumo::Person::getStage(id, nextStageIndex));
                    break;
                }
                case VAR_PARAMETER: {
                    std::string paramName = "";
                    if (!server.readTypeCheckingString(inputStorage, paramName)) {
                        return server.writeErrorStatusCmd(CMD_GET_PERSON_VARIABLE, "Retrieval of a parameter requires its name.", outputStorage);
                    }
                    server.getWrapperStorage().writeUnsignedByte(TYPE_STRING);
                    server.getWrapperStorage().writeString(libsumo::Person::getParameter(id, paramName));
                    break;
                }
                default:
                    return server.writeErrorStatusCmd(CMD_GET_PERSON_VARIABLE, "Get Person Variable: unsupported variable " + toHex(variable, 2) + " specified", outputStorage);
            }
        }
    } catch (libsumo::TraCIException& e) {
        return server.writeErrorStatusCmd(CMD_GET_PERSON_VARIABLE, e.what(), outputStorage);
    }
    server.writeStatusCmd(CMD_GET_PERSON_VARIABLE, RTYPE_OK, "", outputStorage);
    server.writeResponseWithLength(outputStorage, server.getWrapperStorage());
    return true;
}


bool
TraCIServerAPI_Person::processSet(TraCIServer& server, tcpip::Storage& inputStorage,
                                  tcpip::Storage& outputStorage) {
    std::string warning = ""; // additional description for response
    // variable
    int variable = inputStorage.readUnsignedByte();
    if (variable != VAR_PARAMETER
            && variable != ADD
            && variable != APPEND_STAGE
            && variable != REMOVE_STAGE
            && variable != CMD_REROUTE_TRAVELTIME
            && variable != MOVE_TO_XY
            && variable != VAR_SPEED
            && variable != VAR_TYPE
            && variable != VAR_LENGTH
            && variable != VAR_WIDTH
            && variable != VAR_HEIGHT
            && variable != VAR_MINGAP
            && variable != VAR_COLOR
       ) {
        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Change Person State: unsupported variable " + toHex(variable, 2) + " specified", outputStorage);
    }

    try {
        // TODO: remove declaration of c after completion
        MSTransportableControl& c = MSNet::getInstance()->getPersonControl();
        // id
        std::string id = inputStorage.readString();
        // TODO: remove declaration of p after completion
        const bool shouldExist = variable != ADD;
        MSTransportable* p = c.get(id);
        if (p == nullptr && shouldExist) {
            return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Person '" + id + "' is not known", outputStorage);
        }
        // process
        switch (variable) {
            case VAR_SPEED: {
                double speed = 0;
                if (!server.readTypeCheckingDouble(inputStorage, speed)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Setting speed requires a double.", outputStorage);
                }
                // set the speed for all (walking) stages
                libsumo::Person::setSpeed(id, speed);
                // modify the vType so that stages added later are also affected
                TraCIServerAPI_VehicleType::setVariable(CMD_SET_VEHICLE_VARIABLE, variable, p->getSingularType().getID(), server, inputStorage, outputStorage);
            }
            break;
            case VAR_TYPE: {
                std::string vTypeID;
                if (!server.readTypeCheckingString(inputStorage, vTypeID)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "The vehicle type id must be given as a string.", outputStorage);
                }
                libsumo::Person::setType(id, vTypeID);
                break;
            }
            case VAR_COLOR: {
                libsumo::TraCIColor col;
                if (!server.readTypeCheckingColor(inputStorage, col)) {
                    return server.writeErrorStatusCmd(CMD_SET_VEHICLE_VARIABLE, "The color must be given using the according type.", outputStorage);
                }
                libsumo::Person::setColor(id, col);
                break;
            }
            case ADD: {
                if (inputStorage.readUnsignedByte() != TYPE_COMPOUND) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Adding a person requires a compound object.", outputStorage);
                }
                if (inputStorage.readInt() != 4) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Adding a person needs four parameters.", outputStorage);
                }
                std::string vTypeID;
                if (!server.readTypeCheckingString(inputStorage, vTypeID)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "First parameter (type) requires a string.", outputStorage);
                }
                std::string edgeID;
                if (!server.readTypeCheckingString(inputStorage, edgeID)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Second parameter (edge) requires a string.", outputStorage);
                }
                int depart;
                if (!server.readTypeCheckingInt(inputStorage, depart)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Third parameter (depart) requires an integer.", outputStorage);
                }
                double pos;
                if (!server.readTypeCheckingDouble(inputStorage, pos)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Fourth parameter (position) requires a double.", outputStorage);
                }
                libsumo::Person::add(id, edgeID, pos, STEPS2TIME(depart), vTypeID);
            }
            break;
            case APPEND_STAGE: {
                if (inputStorage.readUnsignedByte() != TYPE_COMPOUND) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Adding a person stage requires a compound object.", outputStorage);
                }
                int numParameters = inputStorage.readInt();
                int stageType;
                if (!server.readTypeCheckingInt(inputStorage, stageType)) {
                    return server.writeErrorStatusCmd(CMD_SET_VEHICLE_VARIABLE, "The first parameter for adding a stage must be the stage type given as int.", outputStorage);
                }
                if (stageType == MSTransportable::DRIVING) {
                    // append driving stage
                    if (numParameters != 4) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Adding a driving stage needs four parameters.", outputStorage);
                    }
                    std::string edgeID;
                    if (!server.readTypeCheckingString(inputStorage, edgeID)) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Second parameter (edge) requires a string.", outputStorage);
                    }
                    std::string lines;
                    if (!server.readTypeCheckingString(inputStorage, lines)) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Third parameter (lines) requires a string.", outputStorage);
                    }
                    std::string stopID;
                    if (!server.readTypeCheckingString(inputStorage, stopID)) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Fourth parameter (stopID) requires a string.", outputStorage);
                    }
                    libsumo::Person::appendDrivingStage(id, edgeID, lines, stopID);
                } else if (stageType == MSTransportable::WAITING) {
                    // append waiting stage
                    if (numParameters != 4) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Adding a waiting stage needs four parameters.", outputStorage);
                    }
                    int duration;
                    if (!server.readTypeCheckingInt(inputStorage, duration)) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Second parameter (duration) requires an int.", outputStorage);
                    }
                    std::string description;
                    if (!server.readTypeCheckingString(inputStorage, description)) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Third parameter (description) requires a string.", outputStorage);
                    }
                    std::string stopID;
                    if (!server.readTypeCheckingString(inputStorage, stopID)) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Fourth parameter (stopID) requires a string.", outputStorage);
                    }
                    libsumo::Person::appendWaitingStage(id, STEPS2TIME(duration), description, stopID);
                } else if (stageType == MSTransportable::MOVING_WITHOUT_VEHICLE) {
                    // append walking stage
                    if (numParameters != 6) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Adding a walking stage needs six parameters.", outputStorage);
                    }
                    std::vector<std::string> edgeIDs;
                    if (!server.readTypeCheckingStringList(inputStorage, edgeIDs)) {
                        return server.writeErrorStatusCmd(CMD_SET_VEHICLE_VARIABLE, "Second parameter (edges) route must be defined as a list of edge ids.", outputStorage);
                    }
                    double arrivalPos;
                    if (!server.readTypeCheckingDouble(inputStorage, arrivalPos)) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Third parameter (arrivalPos) requires a double.", outputStorage);
                    }
                    int duration;
                    if (!server.readTypeCheckingInt(inputStorage, duration)) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Fourth parameter (duration) requires an int.", outputStorage);
                    }
                    double speed;
                    if (!server.readTypeCheckingDouble(inputStorage, speed)) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Fifth parameter (speed) requires a double.", outputStorage);
                    }
                    std::string stopID;
                    if (!server.readTypeCheckingString(inputStorage, stopID)) {
                        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Fourth parameter (stopID) requires a string.", outputStorage);
                    }
                    libsumo::Person::appendWalkingStage(id, edgeIDs, arrivalPos, STEPS2TIME(duration), speed, stopID);
                } else {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Invalid stage type for person '" + id + "'", outputStorage);
                }
            }
            break;
            case REMOVE_STAGE: {
                int nextStageIndex = 0;
                if (!server.readTypeCheckingInt(inputStorage, nextStageIndex)) {
                    return server.writeErrorStatusCmd(CMD_GET_PERSON_VARIABLE, "The message must contain the stage index.", outputStorage);
                }
                libsumo::Person::removeStage(id, nextStageIndex);
            }
            break;
            case CMD_REROUTE_TRAVELTIME: {
                if (inputStorage.readUnsignedByte() != TYPE_COMPOUND) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Rerouting requires a compound object.", outputStorage);
                }
                if (inputStorage.readInt() != 0) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "Rerouting should obtain an empty compound object.", outputStorage);
                }
                libsumo::Person::rerouteTraveltime(id);
            }
            break;
            case MOVE_TO_XY: {
                if (inputStorage.readUnsignedByte() != TYPE_COMPOUND) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "MoveToXY person requires a compound object.", outputStorage);
                }
                const int numArgs = inputStorage.readInt();
                if (numArgs != 5) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "MoveToXY person should obtain: edgeID, x, y, angle and keepRouteFlag.", outputStorage);
                }
                // edge ID
                std::string edgeID;
                if (!server.readTypeCheckingString(inputStorage, edgeID)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "The first parameter for moveToXY must be the edge ID given as a string.", outputStorage);
                }
                // x
                double x = 0;
                if (!server.readTypeCheckingDouble(inputStorage, x)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "The second parameter for moveToXY must be the x-position given as a double.", outputStorage);
                }
                // y
                double y = 0;
                if (!server.readTypeCheckingDouble(inputStorage, y)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "The third parameter for moveToXY must be the y-position given as a double.", outputStorage);
                }
                // angle
                double angle = 0;
                if (!server.readTypeCheckingDouble(inputStorage, angle)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "The fourth parameter for moveToXY must be the angle given as a double.", outputStorage);
                }
                int keepRouteFlag = 1;
                if (!server.readTypeCheckingByte(inputStorage, keepRouteFlag)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "The fifth parameter for moveToXY must be the keepRouteFlag given as a byte.", outputStorage);
                }
                libsumo::Person::moveToXY(id, edgeID, x, y, angle, keepRouteFlag);
            }
            break;
            case VAR_PARAMETER: {
                if (inputStorage.readUnsignedByte() != TYPE_COMPOUND) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "A compound object is needed for setting a parameter.", outputStorage);
                }
                //read itemNo
                inputStorage.readInt();
                std::string name;
                if (!server.readTypeCheckingString(inputStorage, name)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "The name of the parameter must be given as a string.", outputStorage);
                }
                std::string value;
                if (!server.readTypeCheckingString(inputStorage, value)) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, "The value of the parameter must be given as a string.", outputStorage);
                }
                libsumo::Person::setParameter(id, name, value);
            }
            break;
            default:
                try {
                    if (!TraCIServerAPI_VehicleType::setVariable(CMD_SET_PERSON_VARIABLE, variable, p->getSingularType().getID(), server, inputStorage, outputStorage)) {
                        return false;
                    }
                } catch (ProcessError& e) {
                    return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, e.what(), outputStorage);
                }
                break;
        }
    } catch (libsumo::TraCIException& e) {
        return server.writeErrorStatusCmd(CMD_SET_PERSON_VARIABLE, e.what(), outputStorage);
    }
    server.writeStatusCmd(CMD_SET_PERSON_VARIABLE, RTYPE_OK, warning, outputStorage);
    return true;
}


/****************************************************************************/
