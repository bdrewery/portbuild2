#!/bin/env

. ${SCRIPTS_PATH}/common.sh

tunneldir=${DATA_DIR}/tunnels
[ -d ${tunneldir} ] || mkdir ${tunneldir}

tunnel_running() {
	local hostname=$1
	pgrep -q -F ${tunneldir}/${hostname}.pid 2>/dev/null && return 0
	return 1
}

tunnel_exists() {
	local hostname=$1
	local hosts
	hosts=`cat ${tunneldir}/hosts 2>/dev/null`
	case $'\n'${hosts}$'\n' in
		*$'\n'"${hostname}"$'\n'*) return 0;;
		**) return 1;;
	esac
}

tunnel_start() {
	local hostname=$1
	tunnel_exists ${hostname} || {
		warn "${hostname} no such tunnel"
		return 1
	}
	tunnel_running ${hostname} && {
		warn "${hostname} already running"
		return 1
	}
	msg "Starting tunnel ${hostname}"
	daemon -f -p ${tunneldir}/${hostname}.pid \
		-c /bin/sh ${SCRIPTS_PATH}/runssh.sh ${hostname}
}

tunnel_stop() {
	local hostname=$1
	tunnel_exists ${hostname} || {
		warn "${hostname} no such tunnel"
		return 1
	}
	tunnel_running ${hostname} || {
		warn "${hostanme} not running ignoring"
		return 1
	}
	msg "Stopping tunenl ${hostname}"
	pkill -F ${tunneldir}/${hostname}.pid
}

set -- $@
case $1 in
	start)
		tunnel_start ${2}
		;;
	startall)
		while read host; do
			tunnel_start ${host}
		done < ${tunneldir}/hosts
		;;
	stop)
		tunnel_stop ${2}
		;;
	stopall)
		while read host; do
			tunnel_stop ${host}
		done < ${tunneldir}/hosts
		;;
	list)
		[ ! -s ${tunneldir}/hosts ] && return
		while read host; do
			echo -n "tunnel ${host}:"
			tunnel_running ${host} || echo -n " not" && echo " running"
		done < ${tunneldir}/hosts
		;;
	add)
		name=$2
		[ -n "${name}" ] || err 1 "no hostname provided"
		tunnel_exists ${name} && err 1 "${hostname} already exists"
		msg "Adding ${name} to the list of hosts"
		echo "${name}" >> ${tunneldir}/hosts
		;;
	del)
		name=$2
		[ -n "${name}" ] || err 1 "no hostname provided"
		tunnel_exists ${name} || "${hostname} does not exists"
		tunnel_running ${hostname} && tunnep_stop ${hostname}
		msg "Deleting ${name}"
		for h in ${hosts}; do
			[ "${name}" = "${h}" ] && continue
			echo "$h" 
		done > ${tunneldir}/hosts
		;;
esac
