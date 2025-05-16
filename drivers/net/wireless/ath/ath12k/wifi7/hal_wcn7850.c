// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hal_desc.h"
#include "hal_wcn7850.h"

void ath12k_hal_rx_desc_get_crypto_header_wcn7850(struct hal_rx_desc *desc,
						  u8 *crypto_hdr,
						  enum hal_encrypt_type encype)
{
	unsigned int key_id;

	switch (encype) {
	case HAL_ENCRYPT_TYPE_OPEN:
		return;
	case HAL_ENCRYPT_TYPE_TKIP_NO_MIC:
	case HAL_ENCRYPT_TYPE_TKIP_MIC:
		crypto_hdr[0] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.wcn7850.mpdu_start.pn[0]);
		crypto_hdr[1] = 0;
		crypto_hdr[2] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.wcn7850.mpdu_start.pn[0]);
		break;
	case HAL_ENCRYPT_TYPE_CCMP_128:
	case HAL_ENCRYPT_TYPE_CCMP_256:
	case HAL_ENCRYPT_TYPE_GCMP_128:
	case HAL_ENCRYPT_TYPE_AES_GCMP_256:
		crypto_hdr[0] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.wcn7850.mpdu_start.pn[0]);
		crypto_hdr[1] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.wcn7850.mpdu_start.pn[0]);
		crypto_hdr[2] = 0;
		break;
	case HAL_ENCRYPT_TYPE_WEP_40:
	case HAL_ENCRYPT_TYPE_WEP_104:
	case HAL_ENCRYPT_TYPE_WEP_128:
	case HAL_ENCRYPT_TYPE_WAPI_GCM_SM4:
	case HAL_ENCRYPT_TYPE_WAPI:
		return;
	}
	key_id = u32_get_bits(__le32_to_cpu(desc->u.wcn7850.mpdu_start.info5),
			      RX_MPDU_START_INFO5_KEY_ID);
	crypto_hdr[3] = 0x20 | (key_id << 6);
	crypto_hdr[4] = HAL_RX_MPDU_INFO_PN_GET_BYTE3(desc->u.wcn7850.mpdu_start.pn[0]);
	crypto_hdr[5] = HAL_RX_MPDU_INFO_PN_GET_BYTE4(desc->u.wcn7850.mpdu_start.pn[0]);
	crypto_hdr[6] = HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.wcn7850.mpdu_start.pn[1]);
	crypto_hdr[7] = HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.wcn7850.mpdu_start.pn[1]);
}

u32 ath12k_hal_rx_h_mpdu_err_wcn7850(struct hal_rx_desc *desc)
{
	u32 info = __le32_to_cpu(desc->u.wcn7850.msdu_end.info13);
	u32 errmap = 0;

	if (info & RX_MSDU_END_INFO13_FCS_ERR)
		errmap |= HAL_RX_MPDU_ERR_FCS;

	if (info & RX_MSDU_END_INFO13_DECRYPT_ERR)
		errmap |= HAL_RX_MPDU_ERR_DECRYPT;

	if (info & RX_MSDU_END_INFO13_TKIP_MIC_ERR)
		errmap |= HAL_RX_MPDU_ERR_TKIP_MIC;

	if (info & RX_MSDU_END_INFO13_A_MSDU_ERROR)
		errmap |= HAL_RX_MPDU_ERR_AMSDU_ERR;

	if (info & RX_MSDU_END_INFO13_OVERFLOW_ERR)
		errmap |= HAL_RX_MPDU_ERR_OVERFLOW;

	if (info & RX_MSDU_END_INFO13_MSDU_LEN_ERR)
		errmap |= HAL_RX_MPDU_ERR_MSDU_LEN;

	if (info & RX_MSDU_END_INFO13_MPDU_LEN_ERR)
		errmap |= HAL_RX_MPDU_ERR_MPDU_LEN;

	return errmap;
}

void ath12k_hal_rx_desc_get_dot11_hdr_wcn7850(struct hal_rx_desc *desc,
					      struct ieee80211_hdr *hdr)
{
	hdr->frame_control = desc->u.wcn7850.mpdu_start.frame_ctrl;
	hdr->duration_id = desc->u.wcn7850.mpdu_start.duration;
	ether_addr_copy(hdr->addr1, desc->u.wcn7850.mpdu_start.addr1);
	ether_addr_copy(hdr->addr2, desc->u.wcn7850.mpdu_start.addr2);
	ether_addr_copy(hdr->addr3, desc->u.wcn7850.mpdu_start.addr3);
	if (__le32_to_cpu(desc->u.wcn7850.mpdu_start.info4) &
			RX_MPDU_START_INFO4_MAC_ADDR4_VALID) {
		ether_addr_copy(hdr->addr4, desc->u.wcn7850.mpdu_start.addr4);
	}
	hdr->seq_ctrl = desc->u.wcn7850.mpdu_start.seq_ctrl;
}

void ath12k_hal_extract_rx_desc_data_wcn7850(struct hal_rx_desc_data *rx_desc_data,
					     struct hal_rx_desc *rx_desc,
					     struct hal_rx_desc *ldesc)
{
	rx_desc_data->msdu_done = ath12k_hal_rx_h_msdu_done_wcn7850(ldesc);
	rx_desc_data->msdu_len = ath12k_hal_rx_h_msdu_len_wcn7850(ldesc);
	rx_desc_data->l3_pad_bytes = ath12k_hal_rx_h_l3pad_wcn7850(ldesc);
	rx_desc_data->is_first_msdu = ath12k_hal_rx_h_first_msdu_wcn7850(ldesc);
	rx_desc_data->is_last_msdu = ath12k_hal_rx_h_last_msdu_wcn7850(ldesc);
	rx_desc_data->phy_meta_data = ath12k_hal_rx_h_freq_wcn7850(rx_desc);
	rx_desc_data->pkt_type = ath12k_hal_rx_h_pkt_type_wcn7850(rx_desc);
	rx_desc_data->bw = ath12k_hal_rx_h_rx_bw_wcn7850(rx_desc);
	rx_desc_data->rate_mcs = ath12k_hal_rx_h_rate_mcs_wcn7850(rx_desc);
	rx_desc_data->nss = hweight8(ath12k_hal_rx_h_nss_wcn7850(rx_desc));
	rx_desc_data->sgi = ath12k_hal_rx_h_sgi_wcn7850(rx_desc);
	rx_desc_data->is_mcbc = ath12k_hal_rx_h_is_da_mcbc_wcn7850(rx_desc);
	rx_desc_data->seq_no = ath12k_hal_rx_h_seq_no_wcn7850(rx_desc);
	rx_desc_data->peer_id = ath12k_hal_rx_h_peer_id_wcn7850(rx_desc);
	rx_desc_data->err_bitmap = ath12k_hal_rx_h_mpdu_err_wcn7850(rx_desc);
	rx_desc_data->is_decrypted = ath12k_hal_rx_h_is_decrypted_wcn7850(rx_desc);
	rx_desc_data->decap_type = ath12k_hal_rx_h_decap_type_wcn7850(rx_desc);
	rx_desc_data->ip_csum_fail = ath12k_hal_rx_h_ip_cksum_fail_wcn7850(rx_desc);
	rx_desc_data->l4_csum_fail = ath12k_hal_rx_h_l4_cksum_fail_wcn7850(rx_desc);
	rx_desc_data->tid = ath12k_hal_rx_h_tid_wcn7850(rx_desc);
	rx_desc_data->addr2_present = ath12k_hal_rxdesc_mac_addr2_valid_wcn7850(rx_desc);
	rx_desc_data->addr2 = ath12k_hal_rxdesc_get_mpdu_start_addr2_wcn7850(rx_desc);
	rx_desc_data->mesh_ctrl_present =
				ath12k_hal_rx_h_mesh_ctl_present_wcn7850(rx_desc);
	rx_desc_data->seq_ctl_valid = ath12k_hal_rx_h_seq_ctrl_valid_wcn7850(rx_desc);
	rx_desc_data->fc_valid = ath12k_hal_rx_h_fc_valid_wcn7850(rx_desc);
	rx_desc_data->enctype = ath12k_hal_rx_h_enctype_wcn7850(rx_desc);
}
