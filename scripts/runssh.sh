#!/bin/sh

mypid=$$

die() {
	trap SIGCHLD
	logger -t tunnelssh "Stopping ${name}"
	# disabling trap
	trap SIGINT
	trap SIGTERM
	trap SIGKILL
	trap EXIT
	pkill -15 -P ${mypid}
	exit 0
}

startssh() {
	logger -t tunnelssh "Starting tunnel ${name}"
	trap sshdead SIGCHLD
	# Only start if the buildmanager port is binded
	bind=`sockstat -4 -l -p 4444 -P tcp | tail -n +2`
	while [ -z "${bind}" ]; do
		sleep 4
		bind=`sockstat -4 -l -p 4444 -P tcp | tail -n +2`
	done
	ssh ${sshopts} -MNn -R 4444:localhost:4444 ${name} &
}

sshdead() {
	trap SIGCHLD
	logger -t tunnelssh "tunnel ${name} dead"
	sleep 5
	trap sshdead SIGCHLD
	startssh
}

trap die SIGINT SIGTERM SIGKILL EXIT

name=$1
startssh
while :; do
	wait
done
