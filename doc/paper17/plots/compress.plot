#TITLE = "Read Times vs. Matchlist Length"
#set title TITLE offset char 0, char -1
set style data linespoints
#set term pdf size 800, 600
set terminal postscript eps enhanced color 'Helvetica,24' lw 2
set pointsize 1.5

set output "compress.eps"

set xlabel "Processes"
set logscale x 2
#set xrange [24:256]
set xtics (24, 40, 60, 120, 160, 240)
set ylabel "Execution Time (sec)"
#set yrange [0:10]
#set ytics (1,5,10)
set key top right

plot 'madness.dat' index 0 using 1:3 title "PDHT" lw 2, \
     'madness.dat' index 1 using 1:3 title "MVAPICH2" lw 2
