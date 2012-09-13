err() {
	ret=$1
	shift
	echo "ERROR>> $@" >&2
	exit $ret
}

warn() {
	echo "WARNING>> $@" >&2
}

msg() {
	echo "====>> $@"
}

msg_n() {
	echo -n "====>> $@"
}
