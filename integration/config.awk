# SPDX-License-Identifier: MIT
# awk -f config.awk port.modules pc.modules pc64 > NEW-CONFIG
# Buffer before output: reject duplicate/ambiguous integration without fragments.
BEGIN { if (errfile == "") errfile = "/fd/2" }
FILENAME != ARGV[3] {
	if ($0 != "devvconsole") module[++modules] = $0
	next
}
{
	line[++lines] = $0
	if ($0 ~ /^dev[ \t]*$/) { dev++; at = lines }
	if ($0 ~ /^misc[ \t]*$/) { misc++; mat = lines }
	if ($0 !~ /^[ \t]*#/)
		for (i=1; i<=NF; i++)
			if ($i ~ /^(devvconsole|vconsole)(-|$)/) existing = 1
}
END {
	if (ARGC != 4 || dev != 1 || misc != 1 || existing || modules == 0) {
		print "vconsole: expected one dev/misc section and no existing driver" > errfile
		exit 1
	}
	for (i=1; i<=lines; i++) {
		print line[i]
		if (i == at) print "\tvconsole"
		# mkdevlist excludes hyphenated dependency fields; first misc fields
		# are module names, not dependency/registration flags.
		if (i == mat)
			for (j=1; j<=modules; j++) print "\t" module[j]
	}
}
