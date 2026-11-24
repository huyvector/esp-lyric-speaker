"""Pinned IDF 4.4 bridge: GetPlayStatus + initial notifications, SDK untouched."""
from pathlib import Path
import sys
source=Path(sys.argv[1]).read_text()
def replace_once(old,new):
 global source
 if source.count(old)!=1:raise SystemExit('AVRCP patch anchor changed: '+old[:90])
 source=source.replace(old,new,1)
replace_once('#include "osi/allocator.h"','#include "osi/allocator.h"\n#include "esp_bt_main.h"\n#include "avrc_sync_wire.h"')
replace_once('    // handle GET_ELEMENT_ATTR response', '''    // Initial notification replies carry current state but must not trigger
    // another registration while this transaction remains pending.
    if (vendor_msg->p_vendor_data[0] == AVRC_PDU_REGISTER_NOTIFICATION &&
            p_meta_msg->code == AVRC_RSP_INTERIM) {
        uint8_t event; uint32_t value;
        if (vinyl_parse_initial_notification(vendor_msg->p_vendor_data,
                vendor_msg->vendor_len, &event, &value)) {
            vinyl_avrc_initial_notification(event, value);
        }
        return;
    }
    // 4.4 lacks a public GetPlayStatus request/response path. Parse the
    // bounded fixed-size response before its older generic parser rejects it.
    if (vendor_msg->vendor_len > 0 && vendor_msg->p_vendor_data &&
            vendor_msg->p_vendor_data[0] == AVRC_PDU_GET_PLAY_STATUS) {
        vinyl_play_status_t sample = {0};
        bool valid = p_meta_msg->code == AVRC_RSP_IMPL_STBL &&
            vinyl_parse_play_status(vendor_msg->p_vendor_data, vendor_msg->vendor_len, &sample);
        vinyl_avrc_status_response(p_meta_msg->label, p_meta_msg->code, valid,
                                   sample.duration, sample.position, sample.state);
        return;
    }
    // handle GET_ELEMENT_ATTR response''')
replace_once('''            // ignore this response''','''            // Handled before the older generic parser, with length checks.''')
replace_once('void btc_avrc_ct_call_handler(btc_msg_t *msg)', '''// Complete the SDK action already reserved for GetPlayStatus.
int vinyl_avrc_get_play_status(uint8_t tl)
{
    if (tl > 15) return ESP_ERR_INVALID_ARG;
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) return ESP_ERR_INVALID_STATE;
    btc_msg_t msg = {0}; btc_avrc_args_t arg = {0};
    msg.sig = BTC_SIG_API_CALL; msg.pid = BTC_PID_AVRC_CT;
    msg.act = BTC_AVRC_STATUS_API_SND_PLAY_STATUS_EVT; arg.md_cmd.tl = tl;
    return btc_transfer_context(&msg, &arg, sizeof arg, NULL, NULL) == BT_STATUS_SUCCESS ? ESP_OK : ESP_FAIL;
}
static void vinyl_send_play_status(uint8_t tl)
{
    if (!btc_avrc_ct_connected_p() || !(btc_rc_cb.rc_features & BTA_AV_FEAT_METADATA)) return;
    tAVRC_COMMAND command = {0}; BT_HDR* packet = NULL;
    command.cmd.pdu = AVRC_PDU_GET_PLAY_STATUS;
    command.cmd.status = AVRC_STS_NO_ERROR;
    // The 4.4 builder initializes the standard 4-byte zero-parameter PDU.
    if (AVRC_BldCommand(&command, &packet) == AVRC_STS_NO_ERROR && packet) {
        BTA_AvMetaCmd(btc_rc_cb.rc_handle, tl, AVRC_CMD_STATUS, packet);
    }
}
void btc_avrc_ct_call_handler(btc_msg_t *msg)''')
replace_once('''    case BTC_AVRC_CT_API_INIT_EVT: {''','''    case BTC_AVRC_STATUS_API_SND_PLAY_STATUS_EVT:
        vinyl_send_play_status(arg->md_cmd.tl);
        break;
    case BTC_AVRC_CT_API_INIT_EVT: {''')
# Avoid dereferencing an empty vendor payload before the new parser can reject it.
replace_once('''    tAVRC_MSG_VENDOR *vendor_msg = &p_meta_msg->p_msg->vendor;
    BTC_TRACE_DEBUG''','''    tAVRC_MSG_VENDOR *vendor_msg = &p_meta_msg->p_msg->vendor;
    if (!vendor_msg->p_vendor_data || vendor_msg->vendor_len < 4) return;
    BTC_TRACE_DEBUG''')
Path(sys.argv[2]).write_text(source)
