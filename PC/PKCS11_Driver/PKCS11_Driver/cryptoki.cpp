#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <iostream>
#include <list>
#include <vector>
#include <time.h>

#include "pkcs11_common.h"
#include "HSM.h"

/********************************
********** DEFINITIONS **********
*********************************/

#define MAX_SESSIONS 10
#define MAX_SLOTS 10

CK_FUNCTION_LIST pkcs11_function_list = {
	{ 2, 20 }, /* Note: NSS/Firefox ignores this version number and uses C_GetInfo() */
	C_Initialize,
	C_Finalize,
	C_GetInfo,
	C_GetFunctionList,
	C_GetSlotList,
	C_GetSlotInfo,
	C_GetTokenInfo,
	C_GetMechanismList,
	C_GetMechanismInfo,
	C_InitToken,
	C_InitPIN,
	C_SetPIN,
	C_OpenSession,
	C_CloseSession,
	C_CloseAllSessions,
	C_GetSessionInfo,
	C_GetOperationState,
	C_SetOperationState,
	C_Login,
	C_Logout,
	C_CreateObject,
	C_CopyObject,
	C_DestroyObject,
	C_GetObjectSize,
	C_GetAttributeValue,
	C_SetAttributeValue,
	C_FindObjectsInit,
	C_FindObjects,
	C_FindObjectsFinal,
	C_EncryptInit,
	C_Encrypt,
	C_EncryptUpdate,
	C_EncryptFinal,
	C_DecryptInit,
	C_Decrypt,
	C_DecryptUpdate,
	C_DecryptFinal,
	C_DigestInit,
	C_Digest,
	C_DigestUpdate,
	C_DigestKey,
	C_DigestFinal,
	C_SignInit,
	C_Sign,
	C_SignUpdate,
	C_SignFinal,
	C_SignRecoverInit,
	C_SignRecover,
	C_VerifyInit,
	C_Verify,
	C_VerifyUpdate,
	C_VerifyFinal,
	C_VerifyRecoverInit,
	C_VerifyRecover,
	C_DigestEncryptUpdate,
	C_DecryptDigestUpdate,
	C_SignEncryptUpdate,
	C_DecryptVerifyUpdate,
	C_GenerateKey,
	C_GenerateKeyPair,
	C_WrapKey,
	C_UnwrapKey,
	C_DeriveKey,
	C_SeedRandom,
	C_GenerateRandom,
	C_GetFunctionStatus,
	C_CancelFunction,
	C_WaitForSlotEvent,
	/*HSM_C_CertGen, // for some reason, uncommenting this ends up in an error (TODO)
	HSM_C_CertGet,
	HSM_C_LogAdd,
	HSM_C_UserDelete,
	HSM_C_UserModify,
	HSM_C_UserAdd*/
};

/********************************
************ GLOBALS ************
*********************************/
bool g_init = CK_FALSE;

p11_slot g_slots[MAX_SLOTS];

std::vector<Device*> devices_list; // the index represents the slotID (so the same device pointer may be in multiple indexes)
std::vector<p11_session*> g_sessions;

/********************************
********** PROTOTYPES **********
*********************************/
void strcpy_bp(void * destination, char * source, size_t dest_size);

/********************************
******** GENERAL PURPOSE ********
*********************************/

/*
	Initializes Cryptoki

	Since we don't support multi-threading, the argument must be NULL

	If OK, returned value is CKR_OK

*/
CK_RV C_Initialize(CK_VOID_PTR pInitArgs)
{
	int i;

	if (pInitArgs != NULL_PTR)
	{
		return CKR_ARGUMENTS_BAD;
	}

	if (g_init)
		return CKR_CRYPTOKI_ALREADY_INITIALIZED;

	// init sessions list
	g_sessions.clear();
	
	// add slot for HSM token
	HSM * g_hsm = new HSM();
	if (!g_hsm->init())
		return CKR_FUNCTION_FAILED;

	devices_list.clear();
	devices_list.push_back(g_hsm);
	g_hsm->addSlot(&g_slots[0], 0);

	for (i = 1; i < MAX_SLOTS; i++)
		devices_list.push_back(NULL);

	g_init = CK_TRUE;

	return CKR_OK;
}

CK_RV C_Finalize(CK_VOID_PTR pReserved)
{
	if (pReserved != NULL_PTR)
		return CKR_ARGUMENTS_BAD;

	// Clean up
	int i;
	for (i = 0; i < MAX_SLOTS; i++)
	{
		if (devices_list.at(i) != NULL)
		{
			delete devices_list.at(i);
			devices_list.at(i) = NULL;
		}
	}

	g_init = CK_FALSE;

	return CKR_OK;
}

CK_RV C_GetInfo(CK_INFO_PTR pInfo)
{
	if (pInfo == NULL_PTR)
		return CKR_ARGUMENTS_BAD;

	memset(pInfo, 0, sizeof(CK_INFO));
	pInfo->cryptokiVersion.major = 2;
	pInfo->cryptokiVersion.minor = 20;
	strcpy_bp(pInfo->manufacturerID, "INESC-ID", sizeof(pInfo->manufacturerID));
	strcpy_bp(pInfo->libraryDescription, "INESC-ID HSM PKCS#11 Interface", sizeof(pInfo->libraryDescription));
	pInfo->libraryVersion.major = 0;
	pInfo->libraryVersion.minor = 1;

	return CKR_OK;
}

CK_RV C_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR ppFunctionList)
{
	if (ppFunctionList == NULL_PTR)
		return CKR_ARGUMENTS_BAD;

	*ppFunctionList = &pkcs11_function_list;

	return CKR_OK;
}

/********************************
*** SLOT AND TOKEN MANAGEMENT ***
*********************************/
CK_RV C_GetSlotList(CK_BBOOL tokenPresent, CK_SLOT_ID_PTR pSlotList, CK_ULONG_PTR pulCount)
{
	int h = 0;

	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	if (pulCount == NULL_PTR)
		return CKR_ARGUMENTS_BAD;

	// All slots
	if (tokenPresent == CK_FALSE)
	{
		if (pSlotList == NULL_PTR)
		{
			// Set pulCount to total slots available
			*pulCount = MAX_SLOTS;

			return CKR_OK;
		}

		// We want the list of slots
		// They must pass the buffer size in pulCount
		if (*pulCount < MAX_SLOTS)
		{
			return CKR_BUFFER_TOO_SMALL;
		}
	}
	
	int c = 0;
	for (h = 0; h < MAX_SLOTS; h++)
	{
		if (tokenPresent == CK_TRUE)
		{
			if (g_slots[h].slotInfo.flags & CKF_TOKEN_PRESENT)
			{
				if (pSlotList != NULL_PTR)
				{
					pSlotList[c] = h;
				}

				c++;
			}
		}
		else
		{
			if (pSlotList != NULL_PTR)
			{
				pSlotList[c] = h;
			}

			// we're getting the list of all slots
			if (tokenPresent == CK_FALSE)
				c++;
		}
	}

	*pulCount = c;

	return CKR_OK;
}

CK_RV C_GetSlotInfo(CK_SLOT_ID slotID, CK_SLOT_INFO_PTR pInfo)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	if (pInfo == NULL_PTR)
		return CKR_ARGUMENTS_BAD;

	if (slotID > MAX_SLOTS)
		return CKR_SLOT_ID_INVALID;

	// Copy slot information
	pInfo->firmwareVersion.major = g_slots[slotID].slotInfo.firmwareVersion.major;
	pInfo->firmwareVersion.minor = g_slots[slotID].slotInfo.firmwareVersion.minor;
	pInfo->flags = g_slots[slotID].slotInfo.flags;
	pInfo->hardwareVersion.major = g_slots[slotID].slotInfo.hardwareVersion.major;
	pInfo->hardwareVersion.minor = g_slots[slotID].slotInfo.hardwareVersion.minor;
	strcpy_bp(pInfo->manufacturerID, (char*)g_slots[slotID].slotInfo.manufacturerID, sizeof(pInfo->manufacturerID));
	strcpy_bp(pInfo->slotDescription, (char*)g_slots[slotID].slotInfo.slotDescription, sizeof(pInfo->slotDescription));

	return CKR_OK;
}

CK_RV C_GetTokenInfo(CK_SLOT_ID slotID, CK_TOKEN_INFO_PTR pInfo)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	if (pInfo == NULL_PTR)
		return CKR_ARGUMENTS_BAD;

	if (slotID > MAX_SLOTS)
		return CKR_SLOT_ID_INVALID;

	p11_slot pSlot = g_slots[slotID];
	if (!(pSlot.slotInfo.flags & CKF_TOKEN_PRESENT))
		return CKR_TOKEN_NOT_PRESENT;

	// Copy token information
	pInfo->firmwareVersion.major = pSlot.token->tokenInfo.firmwareVersion.major;
	pInfo->firmwareVersion.minor = pSlot.token->tokenInfo.firmwareVersion.minor;
	pInfo->hardwareVersion.major = pSlot.token->tokenInfo.hardwareVersion.major;
	pInfo->hardwareVersion.minor = pSlot.token->tokenInfo.hardwareVersion.minor;
	pInfo->flags = pSlot.token->tokenInfo.flags;
	pInfo->hardwareVersion.major = pSlot.token->tokenInfo.hardwareVersion.major;
	pInfo->hardwareVersion.minor = pSlot.token->tokenInfo.hardwareVersion.minor;
	strcpy_bp(pInfo->manufacturerID, (char*)pSlot.token->tokenInfo.manufacturerID, sizeof(pInfo->manufacturerID));
	strcpy_bp(pInfo->model, (char*)pSlot.token->tokenInfo.model, sizeof(pInfo->model));
	strcpy_bp(pInfo->label, (char*)pSlot.token->tokenInfo.label, sizeof(pInfo->label));
	strcpy_bp(pInfo->serialNumber, (char*)pSlot.token->tokenInfo.serialNumber, sizeof(pInfo->serialNumber));
	strcpy_bp(pInfo->utcTime, (char*)pSlot.token->tokenInfo.utcTime, sizeof(pInfo->utcTime));

	pInfo->ulMaxSessionCount = pSlot.token->tokenInfo.ulMaxSessionCount;
	pInfo->ulSessionCount = pSlot.token->tokenInfo.ulSessionCount;
	pInfo->ulMaxRwSessionCount = pSlot.token->tokenInfo.ulMaxRwSessionCount;
	pInfo->ulRwSessionCount = pSlot.token->tokenInfo.ulRwSessionCount;
	pInfo->ulMaxPinLen = pSlot.token->tokenInfo.ulMaxPinLen;
	pInfo->ulMinPinLen = pSlot.token->tokenInfo.ulMinPinLen;
	pInfo->ulTotalPublicMemory = pSlot.token->tokenInfo.ulTotalPublicMemory;
	pInfo->ulTotalPrivateMemory = pSlot.token->tokenInfo.ulTotalPrivateMemory;
	pInfo->ulFreePublicMemory = pSlot.token->tokenInfo.ulFreePublicMemory;
	pInfo->ulFreePrivateMemory = pSlot.token->tokenInfo.ulFreePrivateMemory;

	return CKR_OK;
}

CK_RV C_WaitForSlotEvent(CK_FLAGS flags, CK_SLOT_ID_PTR pSlot, CK_VOID_PTR pReserved)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_GetMechanismList(CK_SLOT_ID slotID, CK_MECHANISM_TYPE_PTR pMechanismList, CK_ULONG_PTR pulCount)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	if (slotID > MAX_SLOTS)
		return CKR_SLOT_ID_INVALID;

	p11_slot pSlot = g_slots[slotID];
	if (!(pSlot.slotInfo.flags & CKF_TOKEN_PRESENT))
		return CKR_TOKEN_NOT_PRESENT;

	// The HSM only support two mechanisms
	// Ideally, this would come from the HSM class
	// Because the idea would be to allow multiple versions of the HSM
	// That extends the Device class
	// This would mean that we'd have to get the mechanism list/info for the verify/sign functions as well (instead of being hardcoded)
	if (pMechanismList == NULL_PTR)
	{
		*pulCount = 2;
		return CKR_OK;
	}

	pMechanismList[0] = CKM_ECDSA;
	pMechanismList[1] = CKM_EC_KEY_PAIR_GEN;

	return CKR_OK;
}

CK_RV C_GetMechanismInfo(CK_SLOT_ID slotID, CK_MECHANISM_TYPE type, CK_MECHANISM_INFO_PTR pInfo)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	if (pInfo == NULL_PTR)
		return CKR_ARGUMENTS_BAD;

	if (slotID > MAX_SLOTS)
		return CKR_SLOT_ID_INVALID;

	p11_slot pSlot = g_slots[slotID];
	if (!(pSlot.slotInfo.flags & CKF_TOKEN_PRESENT))
		return CKR_TOKEN_NOT_PRESENT;

	if (type != CKM_ECDSA && type != CKM_EC_KEY_PAIR_GEN)
	{
		return CKR_MECHANISM_INVALID;
	}

	pInfo->ulMaxKeySize = 384 + 1;
	pInfo->ulMinKeySize = 384 + 1;

	// Ideally, this would come from the HSM class
	if (type == CKM_ECDSA)
		pInfo->flags = CKF_HW | CKF_SIGN | CKF_VERIFY;
	else
		pInfo->flags = CKF_HW | CKF_GENERATE_KEY_PAIR;

	return CKR_OK;
}

CK_RV C_InitToken(CK_SLOT_ID slotID, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen, CK_UTF8CHAR_PTR pLabel)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// Get session device
	Device* d;
	try {
		d = devices_list.at(slotID);
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	if (ulPinLen != 32 || pPin == NULL_PTR)
	{
		return CKR_PIN_INCORRECT;
	}

	if (d->initDevice(pPin, ulPinLen))
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}

CK_RV C_InitPIN(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_SetPIN(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pOldPin, CK_ULONG ulOldLen, CK_UTF8CHAR_PTR pNewPin, CK_ULONG ulNewLen)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// Modify PIN of logged in user

	return CKR_OK;
}

/*************************
*** SESSION MANAGEMENT ***
**************************/
CK_RV C_OpenSession(CK_SLOT_ID slotID, CK_FLAGS flags, CK_VOID_PTR pApplication, CK_NOTIFY Notify, CK_SESSION_HANDLE_PTR phSession)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	if (!(flags & CKF_SERIAL_SESSION))
	{
		// CKR_PARALLEL_NOT_SUPPORTED doesn't seem to exist
		//return CKR_PARALLEL_NOT_SUPPORTED;
	}

	// How many sessions in the selected token?
	try {
		if (devices_list.at(slotID)->sessionLimit())
		{
			// Too many open sessions with device
			return CKR_SESSION_COUNT;
		}
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}
	
	// Start session
	int r = devices_list.at(slotID)->startSession();
	if (r == 1 || g_sessions.size() >= MAX_SESSIONS)
	{
		// Too many open sessions with device
		return CKR_SESSION_COUNT;
	}

	// NOTE
	// If we had more than 1 session allowed, we'd need to check if we have any SO session open
	// Read-Only session can't be opened when a SO session exists

	CK_SESSION_INFO_PTR session = (CK_SESSION_INFO*)malloc(sizeof(CK_SESSION_INFO));
	session->slotID = slotID;
	session->flags = flags;
	session->ulDeviceError = 0;
	session->state = 0;

	p11_session * session_h = (p11_session*)malloc(sizeof(p11_session));
	session_h->session = session;
	session_h->operation[P11_OP_SIGN] = 0;
	session_h->operation[P11_OP_VERIFY] = 0;
	session_h->objects = NULL;
	session_h->totalObjects = 0;

	g_sessions.push_back(session_h);

	// set the handle to the size of the vector-1 (regardless of having empty members or not)
	*phSession = g_sessions.size()-1;
	
	return CKR_OK;
}

CK_RV C_CloseSession(CK_SESSION_HANDLE hSession)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// End session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch(const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}
	
	if (s != NULL_PTR)
	{
		Device* d;
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}

		d->endSession();

		delete g_sessions.at(hSession);
		g_sessions.erase(g_sessions.begin()+hSession);
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	return CKR_OK;
}

CK_RV C_CloseAllSessions(CK_SLOT_ID slotID)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	int r = 0;
	for (std::vector<p11_session*>::iterator it = g_sessions.begin(); it != g_sessions.end(); ++it) {
		/* std::cout << *it; ... */
		r = C_CloseSession(it - g_sessions.begin());
		if (r != CKR_OK)
			return CKR_FUNCTION_FAILED;

		delete g_sessions.at(it - g_sessions.begin());
		g_sessions.at(it - g_sessions.begin()) = NULL_PTR;
	}
	g_sessions.clear();

	return CKR_OK;
}

CK_RV C_GetSessionInfo(CK_SESSION_HANDLE hSession, CK_SESSION_INFO_PTR pInfo)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// TODO

	return CKR_OK;
}

CK_RV C_GetOperationState(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pOperationState, CK_ULONG_PTR pulOperationStateLen)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// TODO

	return CKR_OK;
}

CK_RV C_SetOperationState(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pOperationState, CK_ULONG ulOperationStateLen, CK_OBJECT_HANDLE hEncryptionKey, CK_OBJECT_HANDLE hAuthenticationKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_Login(CK_SESSION_HANDLE hSession, CK_USER_TYPE userType, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	if (userType != CKU_USER && userType != CKU_SO)
	{
		return CKR_USER_TYPE_INVALID;
	}

	// We accept sessions with handle=0
	/*if (hSession == NULL_PTR)
	{
		return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get session device
	Device* d;
	try {
		d = devices_list.at(s->slotID);
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Perform login
	int r = d->login(pPin, ulPinLen, userType);
	if (r == 1)
	{
		return CKR_USER_ALREADY_LOGGED_IN;
	}
	else if (r == 2)
	{
		return CKR_FUNCTION_FAILED;
	}
	else if (r == 3)
	{
		return CKR_PIN_INCORRECT;
	}

	return CKR_OK;
}

CK_RV C_Logout(CK_SESSION_HANDLE hSession)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
		return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get session device
	Device* d;
	try {
		d = devices_list.at(s->slotID);
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Perform login
	int r = d->logout();
	if (r == 1)
	{
		return CKR_USER_NOT_LOGGED_IN;
	}
	else if (r == 2)
	{
		return CKR_FUNCTION_FAILED;
	}
	else if (r == 3)
	{
		return CKR_PIN_INCORRECT;
	}

	return CKR_OK;
}

/*************************
**** OBJECT MANAGEMENT ***
**************************/

CK_RV C_CreateObject(CK_SESSION_HANDLE hSession, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount, CK_OBJECT_HANDLE_PTR phObject)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
		return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	p11_session * P11_SESSION;
	try {
		P11_SESSION = g_sessions.at(hSession);
		s = P11_SESSION->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get session device
	Device* d;
	try {
		d = devices_list.at(s->slotID);
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	if (pTemplate == NULL_PTR)
	{
		return CKR_ARGUMENTS_BAD;
	}

	// Do we have any objects already?
	CK_OBJECT_HANDLE h = P11_SESSION->totalObjects;
	if (P11_SESSION->objects == NULL)
	{
		// Allocate pointer to array of pointers (with one element so far)
		P11_SESSION->objects = (p11_object **)malloc(sizeof(p11_object *));
		if (P11_SESSION->objects == NULL)
		{
			return CKR_HOST_MEMORY;
		}

		P11_SESSION->objects[h] = NULL;
	}
	else
	{
		// We must realloc our array of pointers
		P11_SESSION->objects = (p11_object**)realloc(P11_SESSION->objects, sizeof(p11_object *)*(P11_SESSION->totalObjects+1));
		if (P11_SESSION->objects == NULL)
		{
			// Ideally we'd free our existing objects...

			return CKR_HOST_MEMORY;
		}

		P11_SESSION->objects[h] = NULL;
	}

	// Allocate our object pointer
	P11_SESSION->objects[h] = (p11_object *)malloc(sizeof(p11_object));
	P11_SESSION->objects[h]->oClass = 0;
	P11_SESSION->objects[h]->oType = 0;
	P11_SESSION->objects[h]->oToken = 0;
	P11_SESSION->objects[h]->oValue = NULL;
	P11_SESSION->totalObjects++;

	uint32_t p = 0;
	for (p = 0; p < ulCount; p++)
	{
		if (pTemplate[p].type == CKA_CLASS)
		{
			P11_SESSION->objects[h]->oClass = (*(CK_OBJECT_CLASS*)pTemplate[p].pValue);
		}
		else if (pTemplate[p].type == CKA_KEY_TYPE)
		{
			P11_SESSION->objects[h]->oType = (*(CK_KEY_TYPE*)pTemplate[p].pValue);
		}
		else if (pTemplate[p].type == CKA_TOKEN)
		{
			P11_SESSION->objects[h]->oToken = (*(CK_BBOOL*)pTemplate[p].pValue);
		}
		else if (pTemplate[p].type == CKA_VALUE || pTemplate[p].type == CKA_EC_POINT)
		{
			P11_SESSION->objects[h]->oValue = (CK_BYTE_PTR)malloc(sizeof(CK_BYTE)*pTemplate[p].ulValueLen);
			memcpy(P11_SESSION->objects[h]->oValue, pTemplate[p].pValue, pTemplate[p].ulValueLen * sizeof(CK_BYTE));
			P11_SESSION->objects[h]->oValueLen = pTemplate[p].ulValueLen;
		}
	}

	*phObject = h;

	return CKR_OK;
}

CK_RV C_CopyObject(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount, CK_OBJECT_HANDLE_PTR phNewObject)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_DestroyObject(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	p11_session * P11_SESSION;
	try {
		P11_SESSION = g_sessions.at(hSession);
		s = P11_SESSION->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get session device
	Device* d;
	try {
		d = devices_list.at(s->slotID);
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Do we have any objects already?
	CK_OBJECT_HANDLE h = P11_SESSION->totalObjects;
	if (P11_SESSION->objects == NULL)
	{
		return CKR_OBJECT_HANDLE_INVALID;
	}
	else
	{
		// Delete the selected object
		free(P11_SESSION->objects[hObject]->oValue);
		free(P11_SESSION->objects[hObject]);
		P11_SESSION->objects[hObject] = NULL;
	}

	// DO NOT DECREASE TOTAL OBJECTS BECAUSE WE USE THAT AS HANDLE POINTER

	return CKR_OK;
}

CK_RV C_GetObjectSize(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject, CK_ULONG_PTR pulSize)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_GetAttributeValue(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	p11_session * P11_SESSION;
	try {
		P11_SESSION = g_sessions.at(hSession);
		s = P11_SESSION->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get session device
	Device* d;
	try {
		d = devices_list.at(s->slotID);
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Do we have any objects already?
	if (P11_SESSION->objects == NULL || (P11_SESSION->totalObjects-1) < hObject)
	{
		return CKR_OBJECT_HANDLE_INVALID;
	}
	
	uint32_t p = 0;
	for (p = 0; p < ulCount; p++)
	{
		if (pTemplate[p].type == CKA_CLASS)
		{
			if (pTemplate[p].pValue == NULL_PTR)
			{
				// Modify length to contain value size
				pTemplate[p].ulValueLen = sizeof(P11_SESSION->objects[hObject]->oClass);
			}
			else
			{
				if (pTemplate[p].ulValueLen < sizeof(P11_SESSION->objects[hObject]->oClass))
				{
					pTemplate[p].ulValueLen = -1;
					return CKR_BUFFER_TOO_SMALL;
				}

				CK_OBJECT_CLASS_PTR ptr = (CK_OBJECT_CLASS_PTR)pTemplate[p].pValue;
				*ptr = P11_SESSION->objects[hObject]->oClass;
			}
		}
		else if (pTemplate[p].type == CKA_KEY_TYPE)
		{
			if (pTemplate[p].pValue == NULL_PTR)
			{
				// Modify length to contain value size
				pTemplate[p].ulValueLen = sizeof(P11_SESSION->objects[hObject]->oType);
			}
			else
			{
				if (pTemplate[p].ulValueLen < sizeof(P11_SESSION->objects[hObject]->oType))
				{
					pTemplate[p].ulValueLen = -1;
					return CKR_BUFFER_TOO_SMALL;
				}

				CK_OBJECT_CLASS_PTR ptr = (CK_OBJECT_CLASS_PTR)pTemplate[p].pValue;
				*ptr = P11_SESSION->objects[hObject]->oType;
			}
		}
		else if (pTemplate[p].type == CKA_TOKEN)
		{
			if (pTemplate[p].pValue == NULL_PTR)
			{
				// Modify length to contain value size
				pTemplate[p].ulValueLen = sizeof(P11_SESSION->objects[hObject]->oToken);
			}
			else
			{
				if (pTemplate[p].ulValueLen < sizeof(P11_SESSION->objects[hObject]->oToken))
				{
					pTemplate[p].ulValueLen = -1;
					return CKR_BUFFER_TOO_SMALL;
				}

				CK_OBJECT_CLASS_PTR ptr = (CK_OBJECT_CLASS_PTR)pTemplate[p].pValue;
				*ptr = P11_SESSION->objects[hObject]->oToken;
			}
		}
		else if (pTemplate[p].type == CKA_VALUE || pTemplate[p].type == CKA_EC_POINT)
		{
			if (pTemplate[p].pValue == NULL_PTR)
			{
				// Modify length to contain value size
				pTemplate[p].ulValueLen = sizeof(CK_BYTE)*P11_SESSION->objects[hObject]->oValueLen;
			}
			else
			{
				if (pTemplate[p].ulValueLen < sizeof(CK_BYTE)*P11_SESSION->objects[hObject]->oValueLen)
				{
					pTemplate[p].ulValueLen = -1;
					return CKR_BUFFER_TOO_SMALL;
				}

				memcpy(pTemplate[p].pValue, P11_SESSION->objects[hObject]->oValue, sizeof(CK_BYTE)*P11_SESSION->objects[hObject]->oValueLen);
			}
		}
	}

	return CKR_OK;
}

CK_RV C_SetAttributeValue(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_FindObjectsInit(CK_SESSION_HANDLE hSession, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_FindObjects(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE_PTR phObject, CK_ULONG ulMaxObjectCount, CK_ULONG_PTR pulObjectCount)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_FindObjectsFinal(CK_SESSION_HANDLE hSession)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

/*************************
******* ENCRYPTION *******
**************************/

CK_RV C_EncryptInit(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_Encrypt(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData, CK_ULONG ulDataLen, CK_BYTE_PTR pEncryptedData, CK_ULONG_PTR pulEncryptedDataLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_EncryptUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen, CK_BYTE_PTR pEncryptedPart, CK_ULONG_PTR pulEncryptedPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_EncryptFinal(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pLastEncryptedPart, CK_ULONG_PTR pulLastEncryptedPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

/*************************
******* DECRYPTION *******
**************************/

CK_RV C_DecryptInit(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_Decrypt(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pEncryptedData, CK_ULONG ulEncryptedDataLen, CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_DecryptUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pEncryptedPart, CK_ULONG ulEncryptedPartLen, CK_BYTE_PTR pPart, CK_ULONG_PTR pulPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_DecryptFinal(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pLastPart, CK_ULONG_PTR pulLastPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

/*************************
***** MESSAGE DIGEST *****
**************************/

CK_RV C_DigestInit(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_Digest(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData, CK_ULONG ulDataLen, CK_BYTE_PTR pDigest, CK_ULONG_PTR pulDigestLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_DigestUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_DigestKey(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_DigestFinal(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pDigest, CK_ULONG_PTR pulDigestLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

/*************************
*** CREATE SIGNATURES ****
**************************/

CK_RV C_SignInit(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	if (pMechanism->mechanism != CKM_ECDSA)
		return CKR_MECHANISM_INVALID;

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
		return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	if (s != NULL_PTR)
	{
		Device* d;
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Is the selected session performing data signing?
	if (g_sessions.at(hSession)->operation[P11_OP_SIGN] == 1)
	{
		return CKR_OPERATION_ACTIVE;
	}

	// Key object must be NULL_PTR (the HSM uses internal keys)
	if (hKey != NULL_PTR)
	{
		return CKR_KEY_HANDLE_INVALID;
	}

	// session is performing data signing
	g_sessions.at(hSession)->operation[P11_OP_SIGN] = 1;

	g_sessions.at(hSession)->signData = NULL;
	g_sessions.at(hSession)->signData_s = 0;

	return CKR_OK;
}

CK_RV C_Sign(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData, CK_ULONG ulDataLen, CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Is the selected session performing data signing?
	if (g_sessions.at(hSession)->operation[P11_OP_SIGN] == 0)
	{
		return CKR_OPERATION_NOT_INITIALIZED;
	}

	if (pData == NULL_PTR)
		return CKR_DATA_INVALID;

	if (ulDataLen == 0)
		return CKR_DATA_LEN_RANGE;

	// Did we run signUpdate already?
	if (g_sessions.at(hSession)->update == true)
	{
		return CKR_FUNCTION_FAILED;
	}
	
	// Signature
	if (pSignature == NULL_PTR || *pulSignatureLen < 256)
	{
		*pulSignatureLen = 256; // should handle most signatures...
		return CKR_OK;
	}

	// Validate length
	if (*pulSignatureLen < 256)
	{
		return CKR_BUFFER_TOO_SMALL;
	}

	g_sessions.at(hSession)->operation[P11_OP_SIGN] = 0;

	if (!d->signData(pData, ulDataLen, pSignature, pulSignatureLen))
	{

		return CKR_FUNCTION_FAILED;
	}
	
	return CKR_OK;
}

CK_RV C_SignUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	if (s != NULL_PTR)
	{
		Device* d;
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Is the selected session performing data signing?
	if (g_sessions.at(hSession)->operation[P11_OP_SIGN] == 0)
	{
		return CKR_OPERATION_NOT_INITIALIZED;
	}
	
	// We're doing an update operation
	g_sessions.at(hSession)->update = true;

	// Realloc
	g_sessions.at(hSession)->signData = (uint8_t*)realloc(g_sessions.at(hSession)->signData, g_sessions.at(hSession)->signData_s + ulPartLen);
	if (g_sessions.at(hSession)->signData == NULL)
	{
		return CKR_HOST_MEMORY;
	}

	// Copy data
	memcpy(g_sessions.at(hSession)->signData+ g_sessions.at(hSession)->signData_s, pPart, ulPartLen);

	g_sessions.at(hSession)->signData_s += ulPartLen;

	return CKR_OK;
}

CK_RV C_SignFinal(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Is the selected session performing data signing?
	if (g_sessions.at(hSession)->operation[P11_OP_SIGN] == 0)
	{
		return CKR_OPERATION_NOT_INITIALIZED;
	}

	// Didn't we run signUpdate yet?
	if (g_sessions.at(hSession)->update == false)
	{
		return CKR_FUNCTION_FAILED;
	}

	// Obtain signature
	if (!d->signData(g_sessions.at(hSession)->signData, g_sessions.at(hSession)->signData_s, pSignature, pulSignatureLen))
	{

		return CKR_FUNCTION_FAILED;
	}

	g_sessions.at(hSession)->operation[P11_OP_SIGN] = 0;
	g_sessions.at(hSession)->update = false;

	return CKR_OK;
}

CK_RV C_SignRecoverInit(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_SignRecover(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData, CK_ULONG ulDataLen, CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

/*************************
*** VERIFY SIGNATURES ****
**************************/

CK_RV C_VerifyInit(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	if (pMechanism->mechanism != CKM_ECDSA)
		return CKR_MECHANISM_INVALID;

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	if (s != NULL_PTR)
	{
		Device* d;
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Is the selected session performing digital signatures verification?
	if (g_sessions.at(hSession)->operation[P11_OP_VERIFY] == 1)
	{
		return CKR_OPERATION_ACTIVE;
	}

	// Key object must be NULL_PTR (the HSM uses internal keys)
	if (hKey != NULL_PTR)
	{
		return CKR_KEY_HANDLE_INVALID;
	}

	// session is performing data signing
	g_sessions.at(hSession)->operation[P11_OP_VERIFY] = 1;

	g_sessions.at(hSession)->verifyData = NULL;
	g_sessions.at(hSession)->verifyData_s = 0;

	return CKR_OK;
}

CK_RV C_Verify(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData, CK_ULONG ulDataLen, CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Is the selected session performing data signatures verification?
	if (g_sessions.at(hSession)->operation[P11_OP_VERIFY] == 0)
	{
		return CKR_OPERATION_NOT_INITIALIZED;
	}

	if (pData == NULL_PTR)
		return CKR_DATA_INVALID;

	if (ulDataLen == 0)
		return CKR_DATA_LEN_RANGE;

	// Did we run signUpdate already?
	if (g_sessions.at(hSession)->update == true)
	{
		return CKR_FUNCTION_FAILED;
	}

	g_sessions.at(hSession)->operation[P11_OP_VERIFY] = 0;

	if (!d->verifySignature(pData, ulDataLen, pSignature, ulSignatureLen))
	{
		return CKR_SIGNATURE_INVALID;
	}

	return CKR_OK;
}

CK_RV C_VerifyUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	if (s != NULL_PTR)
	{
		Device* d;
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Is the selected session performing data signatures verification?
	if (g_sessions.at(hSession)->operation[P11_OP_VERIFY] == 0)
	{
		return CKR_OPERATION_NOT_INITIALIZED;
	}

	// We're doing an update operation
	g_sessions.at(hSession)->update = true;

	// Realloc
	g_sessions.at(hSession)->verifyData = (uint8_t*)realloc(g_sessions.at(hSession)->verifyData, g_sessions.at(hSession)->verifyData_s + ulPartLen);
	if (g_sessions.at(hSession)->verifyData == NULL)
	{
		return CKR_HOST_MEMORY;
	}

	// Copy data
	memcpy(g_sessions.at(hSession)->verifyData + g_sessions.at(hSession)->verifyData_s, pPart, ulPartLen);

	g_sessions.at(hSession)->verifyData_s += ulPartLen;

	return CKR_OK;
}

CK_RV C_VerifyFinal(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Is the selected session performing data signatures verification?
	if (g_sessions.at(hSession)->operation[P11_OP_VERIFY] == 0)
	{
		return CKR_OPERATION_NOT_INITIALIZED;
	}

	// Didn't we run verifyUpdate yet?
	if (g_sessions.at(hSession)->update == false)
	{
		return CKR_FUNCTION_FAILED;
	}

	// Obtain signature
	if (!d->verifySignature(g_sessions.at(hSession)->verifyData, g_sessions.at(hSession)->verifyData_s, pSignature, ulSignatureLen))
	{
		return CKR_SIGNATURE_INVALID;
	}

	g_sessions.at(hSession)->operation[P11_OP_VERIFY] = 0;
	g_sessions.at(hSession)->update = false;

	return CKR_OK;
}

CK_RV C_VerifyRecoverInit(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_VerifyRecover(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen, CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

/*************************
** DUAL-FUNCTION CRYPTO **
**************************/

CK_RV C_DigestEncryptUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen, CK_BYTE_PTR pEncryptedPart, CK_ULONG_PTR pulEncryptedPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_DecryptDigestUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pEncryptedPart, CK_ULONG ulEncryptedPartLen, CK_BYTE_PTR pPart, CK_ULONG_PTR pulPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_SignEncryptUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen, CK_BYTE_PTR pEncryptedPart, CK_ULONG_PTR pulEncryptedPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_DecryptVerifyUpdate(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pEncryptedPart, CK_ULONG ulEncryptedPartLen, CK_BYTE_PTR pPart, CK_ULONG_PTR pulPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

/*************************
***** KEY MANAGEMENT *****
**************************/

CK_RV C_GenerateKey(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount, CK_OBJECT_HANDLE_PTR phKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_GenerateKeyPair(CK_SESSION_HANDLE hSession,
	CK_MECHANISM_PTR pMechanism,
	CK_ATTRIBUTE_PTR pPublicKeyTemplate,
	CK_ULONG ulPublicKeyAttributeCount,
	CK_ATTRIBUTE_PTR pPrivateKeyTemplate,
	CK_ULONG ulPrivateKeyAttributeCount,
	CK_OBJECT_HANDLE_PTR phPublicKey,
	CK_OBJECT_HANDLE_PTR phPrivateKey)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
		return CKR_SESSION_HANDLE_INVALID;
	}*/

	if (pMechanism->mechanism != CKM_EC_KEY_PAIR_GEN)
		return CKR_MECHANISM_INVALID;

	if (pPublicKeyTemplate == NULL_PTR || pPrivateKeyTemplate == NULL_PTR)
		return CKR_TEMPLATE_INCONSISTENT;

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Check parameters (we only care about CKA_CLASS and CKA_KEY_TYPE...)
	uint32_t p = 0;
	for (p = 0; p < ulPublicKeyAttributeCount; p++)
	{
		if (pPublicKeyTemplate[p].type == CKA_CLASS)
			if ((*(int*)pPublicKeyTemplate[p].pValue) != CKO_PUBLIC_KEY)
				return CKR_TEMPLATE_INCONSISTENT;

		if (pPrivateKeyTemplate[p].type == CKA_KEY_TYPE)
			if ((*(int*)pPrivateKeyTemplate[p].pValue) != CKK_EC)
				return CKR_TEMPLATE_INCONSISTENT;
	}

	for (p = 0; p < ulPrivateKeyAttributeCount; p++)
	{
		if (pPrivateKeyTemplate[p].type == CKA_CLASS)
			if ((*(int*)pPrivateKeyTemplate[p].pValue) != CKO_PRIVATE_KEY)
				return CKR_TEMPLATE_INCONSISTENT;

		if (pPrivateKeyTemplate[p].type == CKA_KEY_TYPE)
			if ((*(int*)pPrivateKeyTemplate[p].pValue) != CKK_EC)
				return CKR_TEMPLATE_INCONSISTENT;
	}

	CK_OBJECT_HANDLE privateKey;
	CK_OBJECT_HANDLE publicKey;

	// Either return both keys or none (with their CKA_LOCAL attribute set to CK_TRUE)
	if (!d->generateKeyPair(hSession, &privateKey, &publicKey))
	{
		return CKR_FUNCTION_FAILED;
	}

	*phPrivateKey = privateKey;
	*phPublicKey = publicKey;

	return CKR_OK;
}

CK_RV C_WrapKey(CK_SESSION_HANDLE hSession,
	CK_MECHANISM_PTR pMechanism,
	CK_OBJECT_HANDLE hWrappingKey,
	CK_OBJECT_HANDLE hKey,
	CK_BYTE_PTR pWrappedKey,
	CK_ULONG_PTR pulWrappedKeyLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_UnwrapKey(CK_SESSION_HANDLE hSession,
	CK_MECHANISM_PTR pMechanism,
	CK_OBJECT_HANDLE hUnwrappingKey,
	CK_BYTE_PTR pWrappedKey,
	CK_ULONG ulWrappedKeyLen,
	CK_ATTRIBUTE_PTR pTemplate,
	CK_ULONG ulAttributeCount,
	CK_OBJECT_HANDLE_PTR phKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_DeriveKey(CK_SESSION_HANDLE hSession,
	CK_MECHANISM_PTR pMechanism,
	CK_OBJECT_HANDLE hBaseKey,
	CK_ATTRIBUTE_PTR pTemplate,
	CK_ULONG ulAttributeCount,
	CK_OBJECT_HANDLE_PTR phKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

/*************************
*** RANDOM NUMBER GEN ****
**************************/

CK_RV C_SeedRandom(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSeed, CK_ULONG ulSeedLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_GenerateRandom(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pRandomData, CK_ULONG ulRandomLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

/*************************
*** PARALLEL FUNCTIONS ***
**************************/

CK_RV C_GetFunctionStatus(CK_SESSION_HANDLE hSession)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV C_CancelFunction(CK_SESSION_HANDLE hSession)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

/*************************
*** EXTRA FUNCTIONS ***
**************************/

// HSM_C_UserAdd
CK_RV HSM_C_UserAdd(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen, CK_BYTE_PTR uID)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	if (pPin == NULL || ulPinLen <= 0)
	{
		return ERROR_BAD_ARGUMENTS;
	}

	if (!d->addUser(pPin, ulPinLen, uID))
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}

// HSM_C_UserModify
CK_RV HSM_C_UserModify(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	if (pPin == NULL || ulPinLen <= 0)
	{
		return ERROR_BAD_ARGUMENTS;
	}

	if (!d->modifyUser(pPin, ulPinLen))
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}

// HSM_C_UserDelete
CK_RV HSM_C_UserDelete(CK_SESSION_HANDLE hSession, CK_BYTE pUid)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	if (pUid <= 0)
	{
		return ERROR_BAD_ARGUMENTS;
	}

	if (!d->deleteUser(pUid))
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}

// HSM_C_LogAdd
CK_RV HSM_C_LogAdd(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pMessage, CK_ULONG lMessage)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
		return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	if (d->logsAdd(pMessage, lMessage))
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}

// HSM_C_LogVerifyDay
CK_RV HSM_C_LogVerifyDay(CK_SESSION_HANDLE hSession, CK_ULONG lDay, CK_ULONG lMonth, CK_ULONG lYear)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	time_t theTime = time(NULL);
	struct tm aTime;
	localtime_s(&aTime, &theTime);
	int month = aTime.tm_mon + 1; // Month is 0-11 so we add 1
	int year = aTime.tm_year + 1900; // Years since 1900
	uint32_t max_days = 0;
	if (month == 4 || month == 6 || month == 9 || month == 11)
		max_days = 30;
	else if (month == 02)
	{
		bool leapyear = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);

		if (leapyear == 0)
			max_days = 29;
		else
			max_days = 29;
	}
	else
		max_days = 31;

	// If day > max_days or day > calendar_day and month=cur_mon and year=cor_year -> ERROR
	if (lDay > max_days || (lDay > aTime.tm_mday && aTime.tm_mon == lMonth && (aTime.tm_year + 1900) == lYear))
	{
		return ERROR_BAD_ARGUMENTS;
	}

	// Verify day
	if (lYear < 2000 || lYear > (aTime.tm_year+1900))
	{
		return ERROR_BAD_ARGUMENTS;
	}

	// Verify month
	if (lMonth <= 0 || lMonth > 12)
	{
		return ERROR_BAD_ARGUMENTS;
	}

	if (!d->logsVerifyDay(lDay, lMonth, lYear))
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}

// HSM_C_LogVerifyMonth
CK_RV HSM_C_LogVerifyMonth(CK_SESSION_HANDLE hSession, CK_ULONG lMonth, CK_ULONG lYear)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	time_t theTime = time(NULL);
	struct tm aTime;
	localtime_s(&aTime, &theTime);

	// Verify month
	if (lMonth <= 0 || lMonth > 12)
	{
		return ERROR_BAD_ARGUMENTS;
	}

	// Verify year
	if (lYear <= 0 || lYear > 12)
	{
		return ERROR_BAD_ARGUMENTS;
	}

	if (!d->logsVerifyMonth(lMonth, lYear))
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}


// HSM_C_LogVerifyYear
CK_RV HSM_C_LogVerifyYear(CK_SESSION_HANDLE hSession, CK_ULONG lYear)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	time_t theTime = time(NULL);
	struct tm aTime;
	localtime_s(&aTime, &theTime);

	// Verify year
	if (lYear <= 0 || lYear > 12)
	{
		return ERROR_BAD_ARGUMENTS;
	}

	if (!d->logsVerifyYear(lYear))
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}

// HSM_C_LogVerifyChain
CK_RV HSM_C_LogVerifyChain(CK_SESSION_HANDLE hSession)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	if (!d->logsVerifyChain())
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}

// HSM_C_LogCounter
CK_RV HSM_C_LogCounter(CK_SESSION_HANDLE hSession, CK_ULONG_PTR lNumber1, CK_ULONG_PTR lNumber2)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// We accept handle=0
	/*if (hSession == NULL_PTR)
	{
	return CKR_SESSION_HANDLE_INVALID;
	}*/

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	if (d->logsGetCounter(lNumber1, lNumber2))
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}


// HSM_C_CertGen
CK_RV HSM_C_CertGen(CK_SESSION_HANDLE hSession, CK_ATTRIBUTE_PTR publicKeyTemplate, CK_ULONG ulCount, CK_UTF8CHAR_PTR publicKey, CK_UTF8CHAR_PTR certificate, CK_ULONG_PTR bufSize)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	if (publicKey == NULL_PTR)
		return ERROR_BAD_ARGUMENTS;

	if (publicKeyTemplate == NULL_PTR)
		return CKR_TEMPLATE_INCOMPLETE;

	if (!d->genCertificate(publicKeyTemplate, ulCount, publicKey, certificate, bufSize))
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}

// HSM_C_CertGet
CK_RV HSM_C_CertGet(CK_SESSION_HANDLE hSession, CK_BYTE pUid, CK_UTF8CHAR_PTR certificate, CK_ULONG_PTR bufSize)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	if (pUid <= 0)
		return ERROR_BAD_ARGUMENTS;

	if (!d->getCertificate(pUid, &certificate, bufSize))
	{
		return CKR_FUNCTION_FAILED;
	}

	return CKR_OK;
}

// HSM_C_Test
CK_RV HSM_C_SendSecure(CK_SESSION_HANDLE hSession)
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}

	// Get session
	CK_SESSION_INFO_PTR s;
	try {
		s = g_sessions.at(hSession)->session;
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	// Get device
	Device* d;
	if (s != NULL_PTR)
	{
		try {
			d = devices_list.at(s->slotID);
		}
		catch (const std::out_of_range&) {
			// 'out of range' error
			return CKR_SESSION_HANDLE_INVALID;
		}
	}
	else
	{
		return CKR_SESSION_HANDLE_INVALID;
	}

	char * data = "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";
	if (!d->sendData((CK_BYTE_PTR)data, 4096))
		return CKR_FUNCTION_FAILED;

	return CKR_OK;
}

// HSM_C_Test
CK_RV HSM_C_SendPlain()
{
	if (!g_init)
	{
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	}
	
	// Get device
	Device* d;
	try {
		d = devices_list.at(0);
	}
	catch (const std::out_of_range&) {
		// 'out of range' error
		return CKR_SESSION_HANDLE_INVALID;
	}

	char * data = "5itW55Ufr6Z2xS4t3oQSxoQHzIQnKpMSkSXSV5jQUUBvOUQl5W7bVYseoC7jnf37AkAznivb8vEvEaSGLuakhSNfscK6JBqlSnOtg0YLnUqjsLw9zx6fUzKs14Bqi4Ez0ZBKmqNafWxkTfZabCXjHTRklbEtVRk3PNW4qpAgm0xPXgqEscQR7kWQjR2RjivLPRptRZmtlmHz7tO5QKDVtevaLpVcUJ6H3G6JSJtfP4JT1baekAzxZn6hTOhMQj9hvibLwGbjzMjI3pmZIxxIBCZ4D63VDqmX53L1cnVCO4pRpwfkwGghN3MB4QRUysHBxbcmDmvKzllX2BI3u2QaOjNr0uHqDoR4jQs1TXLXVNAre33rAuuxHP1nrJvgvXw0iFaB3oZHvgmRiF1gfOMoFeB1CB2YX7QU2RJJbubhf7KWRSvEo9ozNvSB9rjwHNeBfljrotDul03hK2uZFjyKPXrfTYuDxuHlaepQvYACxJv0GvlMsPr5D1VYHoXJfxTr4KPsgzOJGmWThEuvH0JC4imeLOMzGGwUgzm95OSFhacWt66nMF1KSGpLMRnpyRjRbY5FVeL4CGP07V2gXxcJzyfDV8RMQDKqYZrFh4DJWy70LyI8Flm22QRub4f93AxqbVo7ytxKsIWxZaVOuyA1bZqNk3TeuP7LAkbHsepkFa74yExGZP4TsMRNRiv2vq6UeNWgTPTAckSLSLZW8pDR3h56iO6mOEFpVepvoVzpjMjvkFx7hbpwSbXhgVb2oIwtRU8QHa6NB02t4IDNFzBA9kPWhzSGPQOZ37H5N4a6MXmnrlUUugSSjaYYi7yaDsgL7eR4KRQEMNx0RuV83W8vcGNViiWW43oR2u1uEgKM7DfQ1PgW4fJh6aH0M8oEqAhWBsAeoqIwn71BXAuVuhgjomtq2HT2ssRoPktsiOuFSVYRQDPc3ceFHeuMEW9kkqrtIr7xfcaO03mTYTCHkq30E22nEX1vFoA2Q33v2cElJViK1uQlWeIaj1IOGWjSI22FbAY3LKlfeb49UF5OTtLbfnCmf6rj0GZcseFTxLBEAFT271bLkk6rXRKjCVXA0XW5WqZDKDuiz36LFC8iwKHDFIWfnXr7sNVz1l7tZBoI1gp62AIzPsDbISVglPRR9VsZx2Z7eCwHW6KD76UcMMsNnYHGFq4qvW9hiXNSjZXZO695eafsrmsHjxlKN8gm2tbJPcrAWSgVxFVkZ87xVJ4Wg0ub3iR3FzZji6na4BjQstZ0gfmt6a24Lsz2VCYpEbw0Inhbr1xackDTRqFFbhZLEt7B5Tz4CAwEAx1f0Vbb7TccjqNIE92LYwbqlVTMDIt6keK1TzQXRYDwEuygyl20jzqxZtpkCetQHurGeuFozSBxqVYwFUc62SGTJekOc4zoulpnbVXCveYT5OhHFUE5AWt8ZJtxIsykYYU3zjL17uqHCW0llNpECqTNa2NNQIZkcyOzY7MHX7IhSWIjDnbe49FuWe4jww0GLLHh52XTs574zfqmePte5x57NxxzHvTV7ctp5gugrJIoJzr1qflYcrSlFZt9WEllJVB6q6z1Z2fNxvf1AC8hNA27aoXRlEjFHoMFcGaqIIjl6Uvu44n88g6uN3RWk3LNKAA2u2UULBKGtFoh68pZQ0BV165MO2aY0otVtx64bpWKq30EH5PXvht1mbofVpRQw9yg68J3iZz3jmRMownlDC1xWwXNVxpTykX7hVhEJgGjRblOIpinP9TtBafa7m13y7ltcuYq4KpWb4wlKA0EgpioPZnHMCLkibVV4QUf7gDGrPY8uw8jbrBrmM1Kh563lxb8FWhSHrzIceibBpvDAVpny6YxMXicF4Rjw7FLTgOA8hnm91PlLohG4JT0eeiQNsqHO409HTP5R54AOk4PHhElT4e8a7cRPRe9C6EGtrhpexEat6LCRyctZwVs5zAmRaxCZuvXBFSrAmcPGQu0N6LcmDEsphSbwC3EIo2SmBqLvTfyfeFZM1M7URH0DW9G0xKQY5W9sOe0UkROYcEWNciH8OIRsJ2ZLPZioPXrXllT7W1Wm0z6XrvQmJ849E3WuZ2VNoZBWAH07nnDUOc9nV7szKQj074jKhtQFEky8E0P0CkgH4fq4ykzvqtxDwrt6v9k5IZWm5QXrS4s67xBMYyBm00OkAZSkOstAfGxH35OuXLVOFrW7PEwAFKJ5qNJMpHmFExvJiFSumz0J7WS9FEARwylstBWLk6OiGXjfE4VG52wRAiO91jNTlUf1zzXXFmcBneVuzkR4lwqNwOQwZX8gnC95hCxFTtMK23eAN0ls1XLRvRtRIZjPpMWB1WpgDU8mBCv3osBshfKNVyksqlSRt8ReOy6ElJy3DLianBZs5Pt1obQzaHLO2Ba0luRHRxhJVVvZ06rg3tO5UphuyAbMaAHcUyNNXmkwFq9WCMKDBJ34LhVciKVPSo9iyE7IAiyV6L8NYrXQwNR1WLEvzGzgAwOiq3KgNxNmWOHgg8fjXLlLiXQ5hLxDsQf0ob1xAXD5yNvDSrplEtfOM4Czk32KgKU7YkweCo4DmyPtf4wWQCs6eut3aw3eFUYm9bDW1h8lv8McixuoBmm8IGpmu7xAzPgJPJDfnoPUwpKcx8QlVSki8lA528lsUQ6YHIzfeGMLZHgQVS4wtZhUF5QhGm7aVSpjv79oMa4Mi242NtDNnJ2ukrDOXRL13Tm0IbNFu9MtGrFQC1QC1SG6DXlCR6kBl2nxrHmgRnL6yCsEao9FHt0KT3K09re8MpPeXtWrkiz49lay3PnC9ar9t1yHFgQ26JCH2cn4UufKt4nLtFuqIp7DMtQTv8utP3bj4zuR2GopRIfaMXfnRgOVQnTn9Ojj0n89uwOsH01OjKF1H19V0YGR3LaEkSa4E9jycDkATiB6aHSEXBLCRDmh2W6marnrC71jevgwt9gZHXMU5VAaTA9NH7lR0cytv3lg7uopelm1H9D7cSRiTRMqEYfchkk5kxsgY0vY7iVm7s1KetBxwVh1ol4ccVTSmZ0TZY8qU3xp8Hl1VVuCmpJvw2TNqrpe8RmLFlgZP3hb9GJtMGqSWN7fqjqFIhSHSoYkl0Zua9e1qoSyD8opFmCW9n18YSURxU1ZXBkwlcR18UQGt8zuYgBEaQDfv9q2H0XJMqhD58pH4sNslcRkcJAJZeWvo6ZJn6rHfZXklvKajmZML4hOz5iESUCPrOLYgNrB6fvDwg7IZVrGUqUxFIbtsu2qH63EqZR7s98NvwHqI2ptAc2jBFBnDAHwrOX1b8NqnvPfOzNpv6h4ADeR08UmEKbiC96pQlwJayjcRRU40k9uvpY7eeft9QqGaPAFQ0BffgSEZC246hongS4JrqzIj598eiyEgmjP4Jb1ZYpjOESpFvF87u0Y9mNBeBlSoj4Y31KB4jh3swzNw6nQJ9QNiV3gbvcucZsbaEnpWvnVt1yP46bGIEOyBanfb7fvmtOxvZmqoiQvmjNs0CkLnKm0PUsXgG7SHK1X5Rxhz7KoYw3OAUYLt6j12mzotnHz5cSyh0jtcifnrkjrv7cW32pU6iJcJ14b4D6ep80um7sVJuUQDZujBSLtWwq1Es6YyfA9500jnTSehP0Pgscvay054oH5I2771FZXGZBQjaxzhgzibasyZjtx0SwJ5gALBU9cXABIxbrmAgWCRJTrhmXeEJG6slOG8MFKlTzL0wMZV7PP0VEHQG4unVQzJZ9QViGa8LHzLxGTcLUmk0ulC7gIbuxs6wslSmxbiTSUJ4jj9M4ZtUgyARaPjDSgWkfAv2X198cK28ZNLMfJRnVbAymubwuKljx3vtjxWeDbnVlZkBTvqJQ24UJNC5Tj7wvGwLsvLREWxqPj948g87FOva5LKR2NsIYJftgQ84bnv4x2yF12fcJPKKa1TKIf6E0nNypRQKYHtj2bhAtWUWx06kYPn1vgOiWGSnF6rDeye7WsjWJQuNGe8K1B9cNeNTeGtaz6HgAwvwjSkqoHmYXeLqHml9z7KGUBuMF1n2HZx5CEH1vy3cstYpRJyA2TXxqvsQB4RrK4sg7GkycjKYigYYZ2gTlq3NLTZM5Qbv9";
	if (!d->sendData((CK_BYTE_PTR)data, 4096))
		return CKR_FUNCTION_FAILED;

	return CKR_OK;
}

/*************************
***** OTHER FUNCTIONS ****
**************************/

void strcpy_bp(void * destination, char * source, size_t dest_size)
{
	int c = strlen(source) > dest_size ? dest_size : strlen(source);

	memcpy((char *)destination, source, c);
	dest_size -= c;
	memset((char *)destination + c, ' ', dest_size);
}