#!/bin/bash

DIR=$1

NAME=echoserver-`basename ${DIR}`.eps

TMPDIR=`mktemp -d`

if [ "x$VARIANTS" = "x" ]; then
	VARIANTS="pure-cpu hybrid-cpu hybrid-gpu"
fi
for v in $VARIANTS; do
	FILE=${DIR}/${v}.out
	if [ -f $FILE ]; then
		grep "In rate" ${FILE} | grep -o "[1-9][0-9.]*" > ${TMPDIR}/${v}.ydat
		grep -o "[1-9][0-9]*B" ${FILE} | tr -d B > ${TMPDIR}/${v}.xdat
		paste -d, ${TMPDIR}/${v}.xdat ${TMPDIR}/${v}.ydat > ${TMPDIR}/${v}.csv
	fi
done


function graph()
{
	LOC=$1
	FILENAME=$2
	echo -n '
set terminal eps size 5,1.7
set output "'"$FILENAME"'"

set xlabel "Packet Size (B)"

set ylabel "Mbps"
set yrange [0:1000]
set ytics nomirror
set mytics 2

set key top outside horizontal

set datafile separator ","

set style line 1 lc "light-red" lw 2
set style line 2 lc "dark-green" lw 2
set style line 3 lc "dark-blue" lw 2
set style line 4 lc "light-green" lw 2
set style line 5 lc "dark-red" lw 2
set style line 6 lc "light-blue" lw 2


plot \
'
COUNT=1
for v in $VARIANTS; do
	FILE="${TMPDIR}/${v}.csv"
	if [ ! -f $FILE ]; then
		continue
	fi
	AXES=x1y1
	v=`echo $v | sed 's/hybrid-cpu/CPU/' | sed 's/hybrid-gpu/GPU/'`
#	echo "	'${FILE}' u 1 axes ${AXES} with lines ls (${COUNT}) ti '${v}', \\"
	echo "	'${FILE}' u 1:2 axes ${AXES} with linespoints ls (${COUNT}) pt (${COUNT}) ti '${v}', \\"
#	echo "	'${FILE}' u 1:2 axes ${AXES} with error ls (${COUNT}) pt (${COUNT}%2 +1) notitle, \\"
	((COUNT++))
done


}


graph $TMPDIR $NAME | gnuplot
echo "Generated $NAME"

rm -rf $TMPDIR
