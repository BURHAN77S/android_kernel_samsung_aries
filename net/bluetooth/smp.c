/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/smp.h>

static struct sk_buff *smp_build_cmd(struct l2cap_conn *conn, u8 code,
						u16 dlen, void *data)
{
	struct sk_buff *skb;
	struct l2cap_hdr *lh;
	int len;

	len = L2CAP_HDR_SIZE + sizeof(code) + dlen;

	if (len > conn->mtu)
		return NULL;

	skb = bt_skb_alloc(len, GFP_ATOMIC);
	if (!skb)
		return NULL;

	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->len = cpu_to_le16(sizeof(code) + dlen);
	lh->cid = cpu_to_le16(L2CAP_CID_SMP);

	memcpy(skb_put(skb, sizeof(code)), &code, sizeof(code));

	memcpy(skb_put(skb, dlen), data, dlen);

	return skb;
}

static void smp_send_cmd(struct l2cap_conn *conn, u8 code, u16 len, void *data)
{
	struct sk_buff *skb = smp_build_cmd(conn, code, len, data);

	BT_DBG("code 0x%2.2x", code);

	if (!skb)
		return;

	hci_send_acl(conn->hcon, skb, 0);
}

int smp_conn_security(struct l2cap_conn *conn, __u8 sec_level)
{
	__u8 authreq;

	BT_DBG("conn %p hcon %p level 0x%2.2x", conn, conn->hcon, sec_level);

	switch (sec_level) {
	case BT_SECURITY_MEDIUM:
		/* Encrypted, no MITM protection */
		authreq = HCI_AT_NO_BONDING_MITM;
		break;

	case BT_SECURITY_HIGH:
		/* Bonding, MITM protection */
		authreq = HCI_AT_GENERAL_BONDING_MITM;
		break;

	case BT_SECURITY_LOW:
	default:
		return 1;
	}

	if (conn->hcon->link_mode & HCI_LM_MASTER) {
		struct smp_cmd_pairing cp;
		cp.io_capability = 0x00;
		cp.oob_flag = 0x00;
		cp.max_key_size = 16;
		cp.init_key_dist = 0x00;
		cp.resp_key_dist = 0x00;
		cp.auth_req = authreq;
		smp_send_cmd(conn, SMP_CMD_PAIRING_REQ, sizeof(cp), &cp);
	} else {
		struct smp_cmd_security_req cp;
		cp.auth_req = authreq;
		smp_send_cmd(conn, SMP_CMD_SECURITY_REQ, sizeof(cp), &cp);
	}

	return 0;
}

int smp_sig_channel(struct l2cap_conn *conn, struct sk_buff *skb)
{
	__u8 code = skb->data[0];
	__u8 reason;
	int err = 0;

	skb_pull(skb, sizeof(code));

	switch (code) {
	case SMP_CMD_PAIRING_REQ:
		reason = SMP_PAIRING_NOTSUPP;
		smp_send_cmd(conn, SMP_CMD_PAIRING_FAIL, sizeof(reason),
								&reason);
		err = -EOPNOTSUPP;
		break;

	case SMP_CMD_PAIRING_FAIL:
		break;

	case SMP_CMD_PAIRING_RSP:
	case SMP_CMD_PAIRING_CONFIRM:
	case SMP_CMD_PAIRING_RANDOM:
	case SMP_CMD_ENCRYPT_INFO:
	case SMP_CMD_MASTER_IDENT:
	case SMP_CMD_IDENT_INFO:
	case SMP_CMD_IDENT_ADDR_INFO:
	case SMP_CMD_SIGN_INFO:
	case SMP_CMD_SECURITY_REQ:
	default:
		BT_DBG("Unknown command code 0x%2.2x", code);

		reason = SMP_CMD_NOTSUPP;
		smp_send_cmd(conn, SMP_CMD_PAIRING_FAIL, sizeof(reason),
								&reason);
		err = -EOPNOTSUPP;
	}

	kfree_skb(skb);
	return err;
}