#!/usr/bin/awk

/^python3/{

    run += 1
}

/^Request/ {
    if(run == 1)
        sum1+=$5
    else if(run == 2)
        sum2+=$5
}

END {
        print sum1 " " sum2
}

