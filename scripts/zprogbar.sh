#!/bin/bash
tput civis
cols=$(tput cols)                                                                         
lines=$(( $(tput lines) - 4 ))
maxchars=$(($cols * $lines))
denom=$(($maxchars / 100))
max=$(( 100 * $denom ))
sleep=.5

sp="/-\¦"
sc=0
spin() {
   printf "\b${sp:sc++:1}"
   ((sc==${#sp})) && sc=0                                                                 
}                                                                                         

while [[ $percent -ne 100 ]]
do

text="$(zpool status $1)"
percent=$( echo "$text" | awk '/% done/{$1 = "" ; $2 = "" ; $4 = "" ; $5 ="" ; $6 = "" ; $7 = "" ; $8 = "" ; $9 = "" ; print substr($0, 1, length($0)-7)}')
percent=${percent/./}
while [[ $percent -gt $max ]]
do
    percent=$(( $percent / 10 ));
done
percent=$(( $percent / 10 ));
text=${text//%/%%}
linea="$( echo "$text" | awk '/scan/{print; getline; print}')\n"
lineb="$( echo "$text" | awk '/scan/{getline; getline; print $0}')\n\n"                   
length=$(( $percent * $denom ))                                                           
linec=""
for (( pointer=0 ; $pointer < $length ;
pointer=$(( $pointer + 1 )) )) do linec+="^"; done

lined=""                                                                                  
for (( pointer=$(( $pointer + 1 )); $pointer < $max ;
pointer=$(( $pointer + 1 )) )) do lined+="_"; done

for (( loop=0 ; loop < 180 ; loop=$(( $loop + 1 )) ))                                     
do                                                                                        
tput clear                                                                                
printf "$linea"
printf "$lineb"                                                                           
printf "$linec"
spin
printf "$lined"                                                                           
sleep $sleep                                                                              
done

done                                                                                      
printf "\n"                                                                               
tput cnorm
