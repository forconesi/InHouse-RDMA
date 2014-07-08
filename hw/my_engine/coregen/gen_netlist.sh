#!/bin/sh
coregen -p mac/ -b mac/ten_gig_eth_mac_v10_3.xco
git checkout -- mac/ten_gig_eth_mac_v10_3.xco
coregen -p pcie/ -b pcie/endpoint_blk_plus_v1_15.xco
git checkout -- pcie/endpoint_blk_plus_v1_15.xco
coregen -p rx_buffer/ -b rx_buffer/rx_buffer.xco
git checkout -- rx_buffer/rx_buffer.xco 
coregen -p tx_buffer/ -b tx_buffer/tx_buffer.xco
git checkout -- tx_buffer/tx_buffer.xco
coregen -p xaui/ -b xaui/xaui_v10_4.xco
git checkout -- xaui/xaui_v10_4.xco
#coregen -p xaui_dcm/ -b xaui_dcm/xaui_dcm.xaw
#git checkout -- xaui_dcm/xaui_dcm.xaw
