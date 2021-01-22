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
