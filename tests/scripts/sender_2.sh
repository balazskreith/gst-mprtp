./sender --path1_tx_rtp_port=5016 --path1_tx_rtcp_port=5017 --path1_rx_rtp_port=5018 --path1_rx_rtcp_port=5019 --path2_tx_rtp_port=5020 --path2_tx_rtcp_port=5021 --path2_rx_rtp_port=5022 --path2_rx_rtcp_port=5023 --path3_tx_rtp_port=5024 --path3_tx_rtcp_port=5025 --path3_rx_rtp_port=5026 --path3_rx_rtcp_port=5027 --rtpbin_tx_rtcp_port=5028 --rtpbin_rx_rtcp_port=5029 --logsdir=logs2/ --yuvsrc_file=foreman_cif.yuv --yuvsrc_width=352 --yuvsrc_height=288 --path_1_rx_ip=10.0.0.1 --path_2_rx_ip=10.0.1.1 --path_3_rx_ip=10.0.2.1 --path_1_tx_ip=10.0.0.2 --path_2_tx_ip=10.0.1.2 --path_3_tx_ip=10.0.2.2 --playout_min_rate=35 --playout_max_rate=45 --playout_desired_framenum=1 --playout_spread_factor=1.2 --logging=1 --join_min_th=0 --join_max_th=2500 --join_window_th=60 --join_betha_factor=1.2 --owd_th=200 --discard_th=20 --spike_delay_th=500 --spike_var_th=63 --obsolation_th=200 --lost_th=1000 --rtcp_interval_type=2 --report_timeout=0 --controlling_mode=2 --sending_target=1000000 --path1_active=1 --path2_active=0 --path3_active=0 --fec_interval=0 --fec_min_window=0 --fec_max_window=100 --keep_alive_period=0 