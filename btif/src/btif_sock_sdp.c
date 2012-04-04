/************************************************************************************
 *
 *  Copyright (C) 2009-2012 Broadcom Corporation
 *
 *  This program is the proprietary software of Broadcom Corporation and/or its
 *  licensors, and may only be used, duplicated, modified or distributed
 *  pursuant to the terms and conditions of a separate, written license
 *  agreement executed between you and Broadcom (an "Authorized License").
 *  Except as set forth in an Authorized License, Broadcom grants no license
 *  (express or implied), right to use, or waiver of any kind with respect to
 *  the Software, and Broadcom expressly reserves all rights in and to the
 *  Software and all intellectual property rights therein.
 *  IF YOU HAVE NO AUTHORIZED LICENSE, THEN YOU HAVE NO RIGHT TO USE THIS
 *  SOFTWARE IN ANY WAY, AND SHOULD IMMEDIATELY NOTIFY BROADCOM AND DISCONTINUE
 *  ALL USE OF THE SOFTWARE.
 *
 *  Except as expressly set forth in the Authorized License,
 *
 *  1.     This program, including its structure, sequence and organization,
 *         constitutes the valuable trade secrets of Broadcom, and you shall
 *         use all reasonable efforts to protect the confidentiality thereof,
 *         and to use this information only in connection with your use of
 *         Broadcom integrated circuit products.
 *
 *  2.     TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED
 *         "AS IS" AND WITH ALL FAULTS AND BROADCOM MAKES NO PROMISES,
 *         REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY,
 *         OR OTHERWISE, WITH RESPECT TO THE SOFTWARE.  BROADCOM SPECIFICALLY
 *         DISCLAIMS ANY AND ALL IMPLIED WARRANTIES OF TITLE, MERCHANTABILITY,
 *         NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES,
 *         ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 *         CORRESPONDENCE TO DESCRIPTION. YOU ASSUME THE ENTIRE RISK ARISING OUT
 *         OF USE OR PERFORMANCE OF THE SOFTWARE.
 *
 *  3.     TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL BROADCOM OR
 *         ITS LICENSORS BE LIABLE FOR
 *         (i)   CONSEQUENTIAL, INCIDENTAL, SPECIAL, INDIRECT, OR EXEMPLARY
 *               DAMAGES WHATSOEVER ARISING OUT OF OR IN ANY WAY RELATING TO
 *               YOUR USE OF OR INABILITY TO USE THE SOFTWARE EVEN IF BROADCOM
 *               HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES; OR
 *         (ii)  ANY AMOUNT IN EXCESS OF THE AMOUNT ACTUALLY PAID FOR THE
 *               SOFTWARE ITSELF OR U.S. $1, WHICHEVER IS GREATER. THESE
 *               LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY FAILURE OF
 *               ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.
 *
 ************************************************************************************/

/************************************************************************************
 *
 *  Filename:      btif_hf.c
 *
 *  Description:   Handsfree Profile Bluetooth Interface
 *
 *
 ***********************************************************************************/

#include <hardware/bluetooth.h>
#include <hardware/bt_sock.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#define LOG_TAG "BTIF_SOCK_SDP"
#include "btif_common.h"
#include "btif_util.h"

#include "bd.h"

#include "bta_api.h"


#include "bt_target.h"
#include "gki.h"
#include "hcimsgs.h"
#include "sdp_api.h"
#include "btu.h"
#include "btm_api.h"
#include "btm_int.h"
#include "btif_sock_sdp.h"
#include "utl.h"
#include "../bta/ft/bta_fts_int.h"
#include "../bta/pb/bta_pbs_int.h"
#include "../bta/op/bta_ops_int.h"
#include <cutils/log.h>
#define info(fmt, ...)  LOGI ("%s: " fmt,__FUNCTION__,  ## __VA_ARGS__)
#define debug(fmt, ...) LOGD ("%s: " fmt,__FUNCTION__,  ## __VA_ARGS__)
#define error(fmt, ...) LOGE ("## ERROR : %s: " fmt "##",__FUNCTION__,  ## __VA_ARGS__)
#define asrt(s) if(!(s)) LOGE ("## %s assert %s failed at line:%d ##",__FUNCTION__, #s, __LINE__)




#define RESERVED_SCN_PBS 19
#define RESERVED_SCN_OPS 12


static int add_ftp_sdp(const char *p_service_name, int scn)
{
    tSDP_PROTOCOL_ELEM  protoList [3];
    UINT16              ftp_service = UUID_SERVCLASS_OBEX_FILE_TRANSFER;
    UINT16              browse = UUID_SERVCLASS_PUBLIC_BROWSE_GROUP;
    BOOLEAN             status = FALSE;
    int sdp_handle;

    info("scn: %d, service name: %s", scn, p_service_name);
    tBTA_UTL_COD         cod;


    /* also set cod service bit for now */
    cod.service = BTM_COD_SERVICE_OBJ_TRANSFER;
    utl_set_device_class(&cod, BTA_UTL_SET_COD_SERVICE_CLASS);

    if ((sdp_handle = SDP_CreateRecord()) == 0)
    {
        error("FTS SDP: Unable to register FTP Service");
        return sdp_handle;
    }

    /* add service class */
    if (SDP_AddServiceClassIdList(sdp_handle, 1, &ftp_service))
    {
        /* add protocol list, including RFCOMM scn */
        protoList[0].protocol_uuid = UUID_PROTOCOL_L2CAP;
        protoList[0].num_params = 0;
        protoList[1].protocol_uuid = UUID_PROTOCOL_RFCOMM;
        protoList[1].num_params = 1;
        protoList[1].params[0] = scn;
        protoList[2].protocol_uuid = UUID_PROTOCOL_OBEX;
        protoList[2].num_params = 0;

        if (SDP_AddProtocolList(sdp_handle, 3, protoList))
        {
            status = TRUE;  /* All mandatory fields were successful */

            /* optional:  if name is not "", add a name entry */
            if (*p_service_name != '\0')
                SDP_AddAttribute(sdp_handle,
                                 (UINT16)ATTR_ID_SERVICE_NAME,
                                 (UINT8)TEXT_STR_DESC_TYPE,
                                 (UINT32)(strlen(p_service_name) + 1),
                                 (UINT8 *)p_service_name);

            /* Add in the Bluetooth Profile Descriptor List */
            SDP_AddProfileDescriptorList(sdp_handle,
                                             UUID_SERVCLASS_OBEX_FILE_TRANSFER,
                                             BTA_FTS_DEFAULT_VERSION);

        } /* end of setting mandatory protocol list */
    } /* end of setting mandatory service class */

    /* Make the service browseable */
    SDP_AddUuidSequence (sdp_handle, ATTR_ID_BROWSE_GROUP_LIST, 1, &browse);

    if (!status)
    {
        SDP_DeleteRecord(sdp_handle);
        sdp_handle = 0;
        error("bta_fts_sdp_register FAILED");
    }
    else
    {
        bta_sys_add_uuid(ftp_service); /* UUID_SERVCLASS_OBEX_FILE_TRANSFER */
        error("FTS:  SDP Registered (handle 0x%08x)", sdp_handle);
    }

    return sdp_handle;
}


static int add_spp_sdp(const char *service_name, int scn)
{
#if 0
    tSPP_STATUS         status = SPP_SUCCESS;
    UINT16              serviceclassid = UUID_SERVCLASS_SERIAL_PORT;
    tSDP_PROTOCOL_ELEM  proto_elem_list[SPP_NUM_PROTO_ELEMS];
    int              sdp_handle;

    info("scn %d, service name %s", scn, service_name);

    /* register the service */
    if ((sdp_handle = SDP_CreateRecord()) != FALSE)
    {
        /*** Fill out the protocol element sequence for SDP ***/
        proto_elem_list[0].protocol_uuid = UUID_PROTOCOL_L2CAP;
        proto_elem_list[0].num_params = 0;
        proto_elem_list[1].protocol_uuid = UUID_PROTOCOL_RFCOMM;
        proto_elem_list[1].num_params = 1;

        proto_elem_list[1].params[0] = scn;

        if (SDP_AddProtocolList(sdp_handle, SPP_NUM_PROTO_ELEMS,
            proto_elem_list))
        {
            if (SDP_AddServiceClassIdList(sdp_handle, 1, &serviceclassid))
            {
                if ((SDP_AddAttribute(sdp_handle, ATTR_ID_SERVICE_NAME,
                    TEXT_STR_DESC_TYPE, (UINT32)(strlen(service_name)+1),
                    (UINT8 *)service_name)) == FALSE)

                    status = SPP_ERR_SDP_ATTR;
                else
                {
                    UINT16  list[1];

                    /* Make the service browseable */
                    list[0] = UUID_SERVCLASS_PUBLIC_BROWSE_GROUP;
                    if ((SDP_AddUuidSequence (sdp_handle,  ATTR_ID_BROWSE_GROUP_LIST,
                        1, list)) == FALSE)

                        status = SPP_ERR_SDP_CLASSID;
                }
            }
            else
                status = SPP_ERR_SDP_CLASSID;
        }
        else
            status = SPP_ERR_SDP_PROTO;
    }
    else
        status = SPP_ERR_SDP_REG;

    return spb_handle;
#endif
    return 0;
}
#define BTM_NUM_PROTO_ELEMS 2
static int add_sdp_by_uuid(const char *name,  const uint8_t *service_uuid, UINT16 channel)
{

    UINT32 btm_sdp_handle;

    tSDP_PROTOCOL_ELEM  proto_elem_list[BTM_NUM_PROTO_ELEMS];

    /* register the service */
    if ((btm_sdp_handle = SDP_CreateRecord()) != FALSE)
    {
        /*** Fill out the protocol element sequence for SDP ***/
        proto_elem_list[0].protocol_uuid = UUID_PROTOCOL_L2CAP;
        proto_elem_list[0].num_params = 0;
        proto_elem_list[1].protocol_uuid = UUID_PROTOCOL_RFCOMM;
        proto_elem_list[1].num_params = 1;

        proto_elem_list[1].params[0] = channel;

        if (SDP_AddProtocolList(btm_sdp_handle, BTM_NUM_PROTO_ELEMS,
            proto_elem_list))
        {
            UINT8           buff[48];
            UINT8           *p, *type_buf[1];
            UINT8       type[1], type_len[1];
         p = type_buf[0] = buff;
         type[0] = UUID_DESC_TYPE;

//         UINT8_TO_BE_STREAM  (p, (UUID_DESC_TYPE << 3) | SIZE_SIXTEEN_BYTES);
         ARRAY_TO_BE_STREAM (p, service_uuid, 16);
            type_len[0] = 16;
            if( SDP_AddSequence(btm_sdp_handle, (UINT16) ATTR_ID_SERVICE_CLASS_ID_LIST,
                          1, type, type_len, type_buf) )
//            if (SDP_AddServiceClassIdList(btm_sdp_handle, 1, &service_uuid))
            {
                if ((SDP_AddAttribute(btm_sdp_handle, ATTR_ID_SERVICE_NAME,
                    TEXT_STR_DESC_TYPE, (UINT32)(strlen(name)+1),
                    (UINT8 *)name)) )
                {
                    UINT16  list[1];

                    /* Make the service browseable */
                    list[0] = UUID_SERVCLASS_PUBLIC_BROWSE_GROUP;
                    if ((SDP_AddUuidSequence (btm_sdp_handle,  ATTR_ID_BROWSE_GROUP_LIST,
                        1, list)) )

                        return btm_sdp_handle;
                }
            }
        }
    }

    return 0;
}


/* Realm Character Set */
#define BTA_PBS_REALM_CHARSET   0       /* ASCII */

/* Specifies whether or not client's user id is required during obex authentication */
#define BTA_PBS_USERID_REQ      FALSE
extern const tBTA_PBS_CFG bta_pbs_cfg;
const tBTA_PBS_CFG bta_pbs_cfg =
{
    BTA_PBS_REALM_CHARSET,      /* Server only */
    BTA_PBS_USERID_REQ,         /* Server only */
    (BTA_PBS_SUPF_DOWNLOAD | BTA_PBS_SURF_BROWSE),
    BTA_PBS_REPOSIT_LOCAL,
};

static int add_pbap_sdp(const char* p_service_name, int scn)
{

    tSDP_PROTOCOL_ELEM  protoList [3];
    UINT16              pbs_service = UUID_SERVCLASS_PBAP_PSE;
    UINT16              browse = UUID_SERVCLASS_PUBLIC_BROWSE_GROUP;
    BOOLEAN             status = FALSE;
    UINT32              sdp_handle = 0;
    tBTA_PBS_CFG *p_bta_pbs_cfg = (tBTA_PBS_CFG *)&bta_pbs_cfg;

    info("scn %d, service name %s", scn, p_service_name);

    if ((sdp_handle = SDP_CreateRecord()) == 0)
    {
        error("PBS SDP: Unable to register PBS Service");
        return sdp_handle;
    }

    /* add service class */
    if (SDP_AddServiceClassIdList(sdp_handle, 1, &pbs_service))
    {
        memset( protoList, 0 , 3*sizeof(tSDP_PROTOCOL_ELEM) );
        /* add protocol list, including RFCOMM scn */
        protoList[0].protocol_uuid = UUID_PROTOCOL_L2CAP;
        protoList[0].num_params = 0;
        protoList[1].protocol_uuid = UUID_PROTOCOL_RFCOMM;
        protoList[1].num_params = 1;
        protoList[1].params[0] = scn;
        protoList[2].protocol_uuid = UUID_PROTOCOL_OBEX;
        protoList[2].num_params = 0;

        if (SDP_AddProtocolList(sdp_handle, 3, protoList))
        {
            status = TRUE;  /* All mandatory fields were successful */

            /* optional:  if name is not "", add a name entry */
            if (*p_service_name != '\0')
                SDP_AddAttribute(sdp_handle,
                                 (UINT16)ATTR_ID_SERVICE_NAME,
                                 (UINT8)TEXT_STR_DESC_TYPE,
                                 (UINT32)(strlen(p_service_name) + 1),
                                 (UINT8 *)p_service_name);

            /* Add in the Bluetooth Profile Descriptor List */
            SDP_AddProfileDescriptorList(sdp_handle,
                                             UUID_SERVCLASS_PHONE_ACCESS,
                                             BTA_PBS_DEFAULT_VERSION);

        } /* end of setting mandatory protocol list */
    } /* end of setting mandatory service class */

    /* add supported feature and repositories */
    if (status)
    {
        SDP_AddAttribute(sdp_handle, ATTR_ID_SUPPORTED_FEATURES, UINT_DESC_TYPE,
                  (UINT32)1, (UINT8*)&p_bta_pbs_cfg->supported_features);
        SDP_AddAttribute(sdp_handle, ATTR_ID_SUPPORTED_REPOSITORIES, UINT_DESC_TYPE,
                  (UINT32)1, (UINT8*)&p_bta_pbs_cfg->supported_repositories);

        /* Make the service browseable */
        SDP_AddUuidSequence (sdp_handle, ATTR_ID_BROWSE_GROUP_LIST, 1, &browse);
    }

    if (!status)
    {
        SDP_DeleteRecord(sdp_handle);
        sdp_handle = 0;
        APPL_TRACE_ERROR0("bta_pbs_sdp_register FAILED");
    }
    else
    {
        bta_sys_add_uuid(pbs_service);  /* UUID_SERVCLASS_PBAP_PSE */
        APPL_TRACE_DEBUG1("PBS:  SDP Registered (handle 0x%08x)", sdp_handle);
    }

    return sdp_handle;
}


/* object format lookup table */
static const tBTA_OP_FMT bta_ops_obj_fmt[] =
{
    BTA_OP_VCARD21_FMT,
    BTA_OP_VCARD30_FMT,
    BTA_OP_VCAL_FMT,
    BTA_OP_ICAL_FMT,
    BTA_OP_VNOTE_FMT,
    BTA_OP_VMSG_FMT,
    BTA_OP_OTHER_FMT
};

#define BTA_OPS_NUM_FMTS        7
#define BTA_OPS_PROTOCOL_COUNT  3

#ifndef BTUI_OPS_FORMATS
#define BTUI_OPS_FORMATS            (BTA_OP_VCARD21_MASK | BTA_OP_VCARD30_MASK | \
                                         BTA_OP_VCAL_MASK | BTA_OP_ICAL_MASK | \
                                         BTA_OP_VNOTE_MASK | BTA_OP_VMSG_MASK | \
                                         BTA_OP_ANY_MASK )
#endif

static int add_ops_sdp(const char *p_service_name,int scn)
{


    tSDP_PROTOCOL_ELEM  protoList [BTA_OPS_PROTOCOL_COUNT];
    tOBX_StartParams    start_params;
    UINT16      servclass = UUID_SERVCLASS_OBEX_OBJECT_PUSH;
    int         i, j;
    tBTA_UTL_COD cod;
    tOBX_STATUS status;
    UINT8       desc_type[BTA_OPS_NUM_FMTS];
    UINT8       type_len[BTA_OPS_NUM_FMTS];
    UINT8       *type_value[BTA_OPS_NUM_FMTS];
    UINT16      browse;
    UINT32 sdp_handle;
    tBTA_OP_FMT_MASK    formats = BTUI_OPS_FORMATS;

    info("scn %d, service name %s", scn, p_service_name);

    sdp_handle = SDP_CreateRecord();

    /* add service class */
    if (SDP_AddServiceClassIdList(sdp_handle, 1, &servclass))
    {
        /* add protocol list, including RFCOMM scn */
        protoList[0].protocol_uuid = UUID_PROTOCOL_L2CAP;
        protoList[0].num_params = 0;
        protoList[1].protocol_uuid = UUID_PROTOCOL_RFCOMM;
        protoList[1].num_params = 1;
        protoList[1].params[0] = scn;
        protoList[2].protocol_uuid = UUID_PROTOCOL_OBEX;
        protoList[2].num_params = 0;

        if (SDP_AddProtocolList(sdp_handle, BTA_OPS_PROTOCOL_COUNT, protoList))
        {
            SDP_AddAttribute(sdp_handle,
                (UINT16)ATTR_ID_SERVICE_NAME,
                (UINT8)TEXT_STR_DESC_TYPE,
                (UINT32)(strlen(p_service_name) + 1),
                (UINT8 *)p_service_name);

            SDP_AddProfileDescriptorList(sdp_handle,
                UUID_SERVCLASS_OBEX_OBJECT_PUSH,
                0x0100);
        }
    }

    /* Make the service browseable */
    browse = UUID_SERVCLASS_PUBLIC_BROWSE_GROUP;
    SDP_AddUuidSequence (sdp_handle, ATTR_ID_BROWSE_GROUP_LIST, 1, &browse);

    /* add sequence for supported types */
    for (i = 0, j = 0; i < BTA_OPS_NUM_FMTS; i++)
    {
        if ((formats >> i) & 1)
        {
            type_value[j] = (UINT8 *) &bta_ops_obj_fmt[i];
            desc_type[j] = UINT_DESC_TYPE;
            type_len[j++] = 1;
        }
    }

    SDP_AddSequence(sdp_handle, (UINT16) ATTR_ID_SUPPORTED_FORMATS_LIST,
        (UINT8) j, desc_type, type_len, type_value);

    /* set class of device */
    cod.service = BTM_COD_SERVICE_OBJ_TRANSFER;
    utl_set_device_class(&cod, BTA_UTL_SET_COD_SERVICE_CLASS);

    bta_sys_add_uuid(servclass); /* UUID_SERVCLASS_OBEX_OBJECT_PUSH */

    return sdp_handle;
}





static int add_rfc_sdp_by_uuid(const char* name, const uint8_t* uuid, int scn)
{
   int handle = 0;
   UINT16 uuid_16bit;

   uuid_16bit = (((UINT16)uuid[2]) << 8) | (UINT16)uuid[3];

   info("name:%s, scn:%d, uuid_16bit: %d", name, scn,uuid_16bit);

   int final_scn = get_reserved_rfc_channel(uuid);
   if (final_scn == -1)
   {
       final_scn=scn;
   }

   /*
        Bluetooth Socket API relies on having preregistered bluez sdp records for HSAG, HFAG, OPP & PBAP
        that are mapped to rc chan 10, 11,12 & 19. Today HSAG and HFAG is routed to BRCM AG and are not
        using BT socket API so for now we will need to support OPP and PBAP to enable 3rd party developer
        apps running on BRCM Android.

        To do this we will check the UUID for the requested service and mimic the SDP records of bluez
        upon reception.  See functions add_opush() and add_pbap() in sdptool.c for actual records
    */

   /* special handling for preregistered bluez services (OPP, PBAP) that we need to mimic */

    if (uuid_16bit == UUID_SERVCLASS_OBEX_OBJECT_PUSH)
    {
        handle = add_ops_sdp(name,final_scn);
    }
    else if (uuid_16bit == UUID_SERVCLASS_PBAP_PSE)
    {
        handle = add_pbap_sdp(name, final_scn); //PBAP Server is always 19
    }
    else if(uuid_16bit == UUID_SERVCLASS_OBEX_FILE_TRANSFER)
    {
        APPL_TRACE_EVENT0("Stopping btld ftp serivce when 3-party registering ftp service");
        //BTA_FtsDisable();
        handle = add_sdp_by_uuid(name, uuid, final_scn);
    }
    else
    {
        handle = add_sdp_by_uuid(name, uuid, final_scn);
    }
    return handle;
}

BOOLEAN is_reserved_rfc_channel(int scn)
{
    switch(scn)
    {
        case RESERVED_SCN_PBS:
        case RESERVED_SCN_OPS:
            return TRUE;
    }
    return FALSE;
}


int get_reserved_rfc_channel (const uint8_t* uuid)
{
    UINT16 uuid_16bit;
    uuid_16bit = (((UINT16)uuid[2]) << 8) | (UINT16)uuid[3];
    info("uuid_16bit: %d", uuid_16bit);
    if (uuid_16bit == UUID_SERVCLASS_PBAP_PSE)
    {
      return RESERVED_SCN_PBS;
    }
    else if (uuid_16bit == UUID_SERVCLASS_OBEX_OBJECT_PUSH)
    {
      return RESERVED_SCN_OPS;
    }

    return -1;
}

int add_rfc_sdp_rec(const char* name, const uint8_t* uuid, int scn)
{
    int sdp_handle = 0;
    if(is_uuid_empty(uuid))
    {
        switch(scn)
        {
            case RESERVED_SCN_PBS: // PBAP Reserved port
                add_pbap_sdp(name, scn);
                break;
             case RESERVED_SCN_OPS:
                add_ops_sdp(name,scn);
                break;
            default://serial port profile
                sdp_handle = add_spp_sdp(name, scn);
                break;
        }
    }
    else
        sdp_handle = add_rfc_sdp_by_uuid(name, uuid, scn);
    return sdp_handle;
}

void del_rfc_sdp_rec(int handle)
{
    if(handle != -1 && handle != 0)
        SDP_DeleteRecord( handle );
}
