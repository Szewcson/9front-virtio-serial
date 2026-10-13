# SPDX-License-Identifier: MIT
# Conservative single-section local-boot conversion; emits nothing on error.
{
	if(NR > 48 || length($0) > 4096){ bad=1; next }
	if($0 ~ /^\[/){ bad=1; next }
	if($0 ~ /^bootfile=/){
		kernels++
		if($0 != "bootfile=9pc64") bad=1
		next
	}
	if($0 ~ /^bootargs=/){
		args++
		bootargs=substr($0, 10)
		if(bootargs !~ /^local!/ || length(bootargs) > 1024) bad=1
	}
	if($0 ~ /^(qgavirtio|nobootprompt)=/) next
	line[++count]=$0
	bytes+=length($0)+1
}
END {
	# Leave room for native loader/kernel firmware arguments and the 64-field
	# parser. This intentionally refuses large/complex administrator configs.
	if(bad || kernels != 1 || args != 1 ||
	   bytes+length("bootfile=9pc64qga\nqgavirtio=1\nnobootprompt=")+length(bootargs)+1 > 2048) exit 1
	for(i=1; i<=count; i++) print line[i]
	print "bootfile=9pc64qga"
	print "qgavirtio=1"
	print "nobootprompt=" bootargs
}
