# simpletun
Simple half duplex tunnel

Host A(172.18.0.2)
```bash
./simpletun -s -i tun0 
ip route add a.a.a.a/b dev tun0
```

Host B(172.18.0.1)
```bash
./simpletun -i tun0 -c 172.18.0.2 
ip route add c.c.c.c/d dev tun0
```

Host A ------------------------------------ Host B

On host A, the flow to a.a.a.a/b will go through the tunnel.
On host B, the flow to c.c.c.c/d will go through the tunnel.

....

Why is it half duplex? Because SimpleTun has only one thread and can only handle one direction at a time.


for tunnat:

Host A -----------------Host M------------------- Host B
On host A:
```bash
ifconfig enp0s9 172.16.0.1/24
route add -host 172.18.0.1 gw 172.16.0.2
```

On host M:
```bash
ifconfig enp0s9 172.16.0.2/24
ifconfig enp0s10 172.18.0.2/24
./tunnat -i tun1 -o 172.16.0.1 -m 123.110.0.1
ip rule add from 172.16.0.1 tab tunnat
ip route add default dev tun1
ip route add 123.110.0.1/32 dev tun1
```

On host B:
```bash
ifconfig enp0s10 172.18.0.1/24
route add default gw 172.18.0.2
```

Host A ping Host B
ping 172.18.0.1 
```bash
07:48:02.584525 IP 123.110.0.1 > 172.18.0.1: ICMP echo request, id 5, seq 2167, length 64
07:48:02.584795 IP 172.18.0.1 > 123.110.0.1: ICMP echo reply, id 5, seq 2167, length 64
07:48:03.612804 IP 123.110.0.1 > 172.18.0.1: ICMP echo request, id 5, seq 2168, length 64
07:48:03.613082 IP 172.18.0.1 > 123.110.0.1: ICMP echo reply, id 5, seq 2168, length 64
07:48:04.639537 IP 123.110.0.1 > 172.18.0.1: ICMP echo request, id 5, seq 2169, length 64
07:48:04.639939 IP 172.18.0.1 > 123.110.0.1: ICMP echo reply, id 5, seq 2169, length 64
07:48:05.667918 IP 123.110.0.1 > 172.18.0.1: ICMP echo request, id 5, seq 2170, length 64
07:48:05.668204 IP 172.18.0.1 > 123.110.0.1: ICMP echo reply, id 5, seq 2170, length 64
```

enjoy yourself!
