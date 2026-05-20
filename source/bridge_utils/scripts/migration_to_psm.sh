#!/bin/sh
#
# If not stated otherwise in this file or this component's LICENSE file the
# following copyright and licenses apply:
#
# Copyright 2015 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

source /etc/utopia/service.d/log_capture_path.sh
. /etc/device.properties

counter=0

while [ ! -f /tmp/psm_initialized ]
do
  if [ "$counter" -le 30 ]; then
  	echo "`date`: Waiting for psm to be ready.."
  	counter=$((counter+1))
  	sleep 1
  else
  	break
  fi
done

PORT2ENABLE=`syscfg get HomeSecurityEthernet4Flag`
BRIDGE_MODE=`syscfg get bridge_mode`
migrationCompleteFlag=0
cbr2_migrationCompleteFlag=0
MIGRATION_FILE="/nvram/.migration_to_psm_complete"
if [ "xcompleted" != "x`syscfg get psm_migration`" ];then
	if [ "$MODEL_NUM" = "CGM4140COM" ] || [ "$MODEL_NUM" = "CGM4981COM" ] || [ "$MODEL_NUM" == "CGM601TCOM" ] ||  [ "$MODEL_NUM" == "SG417DBCT" ] || [ "$MODEL_NUM" == "CWA438TCOM" ] || [ "$MODEL_NUM" = "CGM4331COM" ];then
		rm -rf "$MIGRATION_FILE"
		psmcli set dmsb.l2net.1.Members.SW ""
		psmcli set dmsb.l2net.9.Members.SW ""
		if [ "$MODEL_NUM" = "CGM4331COM" ] || [ "$MODEL_NUM" = "CGM4981COM" ] || [ "$MODEL_NUM" == "CGM601TCOM" ] || [ "$MODEL_NUM" == "SG417DBCT" ] || [ "$MODEL_NUM" == "CWA438TCOM" ];then
			psmcli set dmsb.l2net.6.Members.WiFi "wl0.3 wl1.3 wl0.5 wl1.5"
		else
			psmcli set dmsb.l2net.6.Members.WiFi "ath6 ath7 ath10 ath11"
		fi
		psmcli set dmsb.l2net.1.Members.Moca "moca0"
		psmcli set dmsb.l2net.9.Members.Moca "moca0"
		for i in 1 2 3 4 5 6 7 8 9
		do
			psmcli set dmsb.l2net."$i".Members.Link ""
			greIf=`psmcli get dmsb.l2net."$i".Members.Gre`
			if [ x"$greIf" != "x" ];then
				psmcli set dmsb.l2net."$i".Members.Gre `echo $greIf | cut -d "-" -f1`
			fi
		done
		if [ "$PORT2ENABLE" = "1" ];then
			if [ "$MODEL_NUM" = "CGM4331COM" ] || [ "$MODEL_NUM" = "CGM4981COM" ] || [ "$MODEL_NUM" == "CGM601TCOM" ] || [ "$MODEL_NUM" == "SG417DBCT" ];then
				psmcli set dmsb.l2net.1.Members.Eth "eth0 eth2 eth3"
			elif [ "$MODEL_NUM" = "CWA438TCOM" ];then
				psmcli set dmsb.l2net.1.Members.Eth "eth0 eth2"
			else
				psmcli set dmsb.l2net.1.Members.Eth "eth0"
			fi
			psmcli set dmsb.l2net.2.Members.Eth "eth1"
		else
			if [ "$MODEL_NUM" = "CGM4331COM" ] || [ "$MODEL_NUM" = "CGM4981COM" ] || [ "$MODEL_NUM" == "CGM601TCOM" ] || [ "$MODEL_NUM" == "SG417DBCT" ];then
                        	psmcli set dmsb.l2net.1.Members.Eth "eth0 eth1 eth2 eth3"
			elif [ "$MODEL_NUM" = "CWA438TCOM" ];then
				psmcli set dmsb.l2net.1.Members.Eth "eth0 eth1 eth2"
                        else
				psmcli set dmsb.l2net.1.Members.Eth "eth0 eth1"
			fi
			psmcli set dmsb.l2net.2.Members.Eth ""
		fi 

                if [ "$MODEL_NUM" == "CGM601TCOM" ] ||  [ "$MODEL_NUM" == "SG417DBCT" ];then
                    if [ ! -f "/nvram/.xb10_enable_ethwan_mode" ];then
                        psmcli set dmsb.wanmanager.if.2.Selection.Enable "FALSE"
                    fi
                fi

		psmcli set dmsb.l2net.1.Port.9.LinkName ""
		psmcli set dmsb.l2net.1.Port.9.Name ""
		psmcli set dmsb.l2net.1.Port.8.LinkName "eth1"
		psmcli set dmsb.l2net.1.Port.8.Name "eth1"
		psmcli set dmsb.l2net.2.Port.2.Name "eth1"
		psmcli set dmsb.l2net.2.Port.2.LinkName "eth1"
		psmcli get dmsb.l2net.2.Members.SW ""
		
		migrationCompleteFlag=1
	fi
	if [ "$MODEL_NUM" = "TG3482G" ];then
		for i in 1 2 3 4 5 6 7 8 9
		do
			psmcli set dmsb.l2net."$i".Members.Link ""
			greIf=`psmcli get dmsb.l2net."$i".Members.Gre`
			if [ x"$greIf" != "x" ];then
				psmcli set dmsb.l2net."$i".Members.Gre `echo $greIf | cut -d "-" -f1`
			fi
		done
		if [ "$BRIDGE_MODE" -gt 0 ];then
			psmcli set dmsb.l2net.1.Members.Eth "llan0 lbr0"
		else
			psmcli set dmsb.l2net.1.Members.Eth "lbr0 "
			psmcli set dmsb.l2net.1.Port.7.Enable "FALSE"
		fi
		psmcli set dmsb.l2net.1.Port.7.LinkName "llan0"
		psmcli set dmsb.l2net.1.Port.7.Name "llan0"
		psmcli set dmsb.l2net.2.Members.Moca ""
		psmcli set dmsb.l2net.3.Members.Moca ""
		psmcli set dmsb.l2net.4.Members.Moca ""
		psmcli set dmsb.l2net.9.Members.Moca "nmoca0"

		
		migrationCompleteFlag=1
	fi

	if [ "$MODEL_NUM" = "TG4482A" ];then
		for i in 1 2 3 4 5 6 7 8 9
		do
			psmcli set dmsb.l2net."$i".Members.Link ""
			greIf=`psmcli get dmsb.l2net."$i".Members.Gre`
			if [ x"$greIf" != "x" ];then
				psmcli set dmsb.l2net."$i".Members.Gre `echo $greIf | cut -d "-" -f1`
			fi
		done
		if [ "$BRIDGE_MODE" -gt 0 ];then
			psmcli set dmsb.l2net.1.Members.Eth "llan0 lbr0 nrgmii2 nsgmii0"
		else
			psmcli set dmsb.l2net.1.Members.Eth "lbr0 nrgmii2 nsgmii0"
			psmcli set dmsb.l2net.1.Port.9.Enable "FALSE"
		fi
 		psmcli set dmsb.l2net.2.Members.Moca ""
		psmcli set dmsb.l2net.3.Members.Moca ""
		psmcli set dmsb.l2net.4.Members.Moca ""
		psmcli set dmsb.l2net.1.Port.9.LinkName "llan0"
		psmcli set dmsb.l2net.1.Port.9.Name "llan0"
		psmcli set dmsb.l2net.2.Members.WiFi "wlan0.1"
		psmcli set dmsb.l2net.3.Members.Gre ""
		psmcli set dmsb.l2net.5.Members.WiFi ""
		psmcli set dmsb.l2net.11.Members.Link "nrgmii2 nsgmii0 sw_2 sw_3"
                psmcli set dmsb.MultiLAN.EthBhaul_l3net 8

		migrationCompleteFlag=1
	fi

fi
#if device is FR in other builds which is not having bridgeUtil or OVS support, device will have wrong psm config. 
#need to correct psm config 
if [ "$migrationCompleteFlag" -eq 0 ];then
	if [ "$MODEL_NUM" = "CGM4140COM" ] || [ "$MODEL_NUM" = "TG3482G" ] || [ "$MODEL_NUM" = "CGM4981COM" ] || [ "$MODEL_NUM" == "CGM601TCOM" ] || [ "$MODEL_NUM" == "SG417DBCT" ] || [ "$MODEL_NUM" == "CWA438TCOM" ] || [ "$MODEL_NUM" = "CGM4331COM" ] || [ "$MODEL_NUM" = "TG4482A" ];then
		for i in 1 2
		do
			if [ "xl2sd0-t" = "x`psmcli get dmsb.l2net."$i".Members.Link`" ];then
				psmcli set dmsb.l2net."$i".Members.Link ""
			fi
		done
	fi
	if [ "$MODEL_NUM" = "TG4482A" ];then
		if [ "x" = "x`psmcli get dmsb.l2net.11.Members.Link`" ];then
			psmcli set dmsb.l2net.11.Members.Link "nrgmii2 nsgmii0 sw_2 sw_3"
		fi
                if [ "8" != "`psmcli get dmsb.MultiLAN.EthBhaul_l3net`" ];then
                        psmcli set dmsb.MultiLAN.EthBhaul_l3net 8
                fi
	fi
fi

	if [ "$MODEL_NUM" = "TG3482G" ] || [ "$MODEL_NUM" = "TG4482A" ] ;then
		if [ ! -f "/nvram/.updatepsm_port_routermode" ];then
			LLAN_PORT=$(psmcli get dmsb.MultiLAN.PrimaryLAN_brport)
			if [ "$BRIDGE_MODE" -eq 0 ];then
				psmcli set dmsb.l2net.1.Port."$LLAN_PORT".Enable "FALSE"
				psmcli set dmsb.l2net.1.Port."$LLAN_PORT".Name "llan0"
			fi
			touch /nvram/.updatepsm_port_routermode
		fi
	fi

if [ "xcompleted" != "x`syscfg get cbrv2_psm_migration_v2`" ] || [ "xcompleted" == "x`syscfg get cbrv2_psm_migration_v1`" ] || [ "xcompleted" == "x`syscfg get cbrv2_psm_migration`" ] || [ "xcompleted" == "x`syscfg get cbr2_psm_migration`" ];then

        if [ "$MODEL_NUM" = "CGA4332COM" ];then
                rm -rf "$MIGRATION_FILE"
                psmcli set dmsb.l2net.1.Members.SW ""
                psmcli set dmsb.l2net.1.Members.Moca ""
                psmcli set dmsb.l2net.1.Members.WiFi "wl0 wl1"
                psmcli set dmsb.l2net.1.Port.6.Name "wl0"
                psmcli set dmsb.l2net.1.Port.6.LinkName "wl0"
                psmcli set dmsb.l2net.1.Members.Eth "eth0 eth1 eth2 eth3 eth4 eth5"

                for i in 1 2 3 4 5 6 7 8
                do
                        psmcli set dmsb.l2net."$i".Members.Link ""
                done
                psmcli set dmsb.l2net.1.Port.7.Name "wl1"
                psmcli set dmsb.l2net.1.Port.7.LinkName "wl1"
                psmcli set dmsb.l2net.1.Port.8.Name "eth1"
                psmcli set dmsb.l2net.1.Port.8.LinkName "eth1"
                psmcli set dmsb.l2net.1.Port.9.LinkName ""
                psmcli set dmsb.l2net.2.Members.WiFi "wl0.1 wl1.1"
                psmcli set dmsb.l2net.2.Port.2.Name "eth1"
                psmcli set dmsb.l2net.2.Port.2.LinkName "eth1"
                psmcli set dmsb.l2net.2.Port.3.Name "wl0.1"
                psmcli set dmsb.l2net.2.Port.3.LinkName "wl0.1"
                psmcli set dmsb.l2net.3.Members.WiFi "wl0.2"
                psmcli set dmsb.l2net.3.Port.2.Name "wl0.2"
                psmcli set dmsb.l2net.3.Port.2.LinkName "wl0.2"
                psmcli set dmsb.l2net.4.Members.WiFi "wl1.2"
                psmcli set dmsb.l2net.4.Port.2.Name "wl1.2"
                psmcli set dmsb.l2net.4.Port.2.LinkName "wl1.2"
                psmcli set dmsb.l2net.7.Members.WiF "wl0.4"
                psmcli set dmsb.l2net.7.Port.2.Name "wl0.4"
                psmcli set dmsb.l2net.7.Port.2.LinkName "wl0.4"
                psmcli set dmsb.l2net.8.Members.WiFi "wl1.4"
                psmcli set dmsb.l2net.8.Port.2.Name "wl1.4"
                psmcli set dmsb.l2net.8.Port.2.LinkName "wl1.4"
                psmcli set dmsb.l2net.5.Name "brlan7"
                psmcli set dmsb.l2net.5.Vid "107"
                psmcli set dmsb.l2net.5.Members.WiFi "wl0.7 wl1.7"
                psmcli set dmsb.l2net.5.Port.1.Pvid "107"
                psmcli set dmsb.l2net.5.Port.1.Name "brlan7"
                psmcli set dmsb.l2net.5.Port.2.Name "wl0.7"
                psmcli set dmsb.l2net.5.Port.2.LinkName "wl0.7"
                psmcli set dmsb.l2net.5.Port.3.Pvid "107"
                psmcli set dmsb.l2net.5.Port.3.Name "wl1.7"
                psmcli set dmsb.l2net.5.Port.3.LinkName "wl1.7"

                psmcli set dmsb.hotspot.tunnel.1.interface.5.AssociatedBridges "Device.Bridging.Bridge.11."
                psmcli set dmsb.hotspot.tunnel.1.interface.5.AssociatedBridgesWiFiPort "Device.Bridging.Bridge.11.Port.2."

                psmcli set dmsb.l2net.3.Members.Gre "gretap0"
                psmcli set dmsb.l2net.4.Members.Gre "gretap0"
                psmcli set dmsb.l2net.7.Members.Gre "gretap0"
                psmcli set dmsb.l2net.8.Members.Gre "gretap0"
                psmcli set dmsb.l2net.11.Members.Gre "gretap0"
                psmcli set dmsb.l2net.11.Members.Link "l2sd0"
                psmcli set dmsb.l2net.11.Vid "2346"
                psmcli set dmsb.l2net.11.Name "brpublic"
                psmcli set dmsb.l2net.11.Alias "Hotspot Network 5"
                psmcli set dmsb.l2net.11.Members.WiFi "wl1.7"

                psmcli del dmsb.l2net.16.Vid
                psmcli del dmsb.l2net.16.Standard
                psmcli del dmsb.l2net.16.Alias
                psmcli del dmsb.l2net.16.Type

                psmcli del dmsb.l2net.16.Members.Eth
                psmcli del dmsb.l2net.16.Members.SW
                psmcli del dmsb.l2net.16.Members.WiFi
                psmcli del dmsb.l2net.16.Members.Gre
                psmcli del dmsb.l2net.16.Members.Moca
                psmcli del dmsb.l2net.16.Members.Link
                psmcli del dmsb.l2net.16.PriorityTag
                psmcli del dmsb.l2net.16.AllowDelete
                psmcli del dmsb.l2net.16.Enable
                psmcli del dmsb.l2net.16.Vlan.1.InstanceNum
                psmcli del dmsb.l2net.16.Vlan.1.Alias
                psmcli del dmsb.l2net.16.InstanceNum
                psmcli del dmsb.l2net.16.Name

                for i in 1 2 3
                do

                  psmcli del dmsb.l2net.16.Port.$i.Management
                  psmcli del dmsb.l2net.16.Port.$i.Mode
                  psmcli del dmsb.l2net.16.Port.$i.Pvid
                  psmcli del dmsb.l2net.16.Port.$i.LinkType
                  psmcli del dmsb.l2net.16.Port.$i.Alias
                  psmcli del dmsb.l2net.16.Port.$i.PriorityTag
                  psmcli del dmsb.l2net.16.Port.$i.Upstream
                  psmcli del dmsb.l2net.16.Port.$i.AllowDelete
                  psmcli del dmsb.l2net.16.Port.$i.Enable
                  psmcli del dmsb.l2net.16.Port.$i.InstanceNum
                  psmcli del dmsb.l2net.16.Port.$i.Name
                  psmcli del dmsb.l2net.16.Port.$i.LinkName

                done

                cbr2_migrationCompleteFlag=1
        fi
        if [ "$cbr2_migrationCompleteFlag" -eq 1 ];then
                syscfg set cbrv2_psm_migration_v2 "completed"
                if [ "xcompleted" == "x`syscfg get cbr2_psm_migration`" ] || [ "xcompleted" == "x`syscfg get cbrv2_psm_migration`" ] || [ "xcompleted" == "x`syscfg get cbrv2_psm_migration_v1`" ];then
                      syscfg unset cbr2_psm_migration
                      syscfg unset cbrv2_psm_migration
                      syscfg unset cbrv2_psm_migration_v1
                fi
                syscfg commit
        fi
fi

exit 0
