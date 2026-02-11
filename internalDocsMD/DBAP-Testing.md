### 2/10/26

# AlloSphere Testing

- For the AlloSphere, Dbap focus 1.0 has a better soundquality than 1.5, with slightly less localization.
- LBAP sounds great but is rendering positions incorrectly, need to investigate rendering code

#### 2/3/26

### Configuring focus parameter

# Testing on Mix of "Swale" in Translab. LFE routed to 2 subs. [8.8.2 speaker config]

Focus Levels:

1 = Best Level Balance, Not very Localized
1.5 = Sweet Spot - Localized but slightly dispersed,mix between sub and loudpseakers needs to be adjusted
2 = Strong localization but levels need to be adjusted
2.5 = Mix Starts to get Muddy

Takeaways:

- make 1.5 default for now
- Conduct further testing to find ideal range between 1.1 and 1.5
- subs need to be updated -- energy needs to be distributed based on number of subs

### LFE Updates

- updated sub rendering block to divide output energy by number of sub channels.
- due to DBAP focus being increaased, the loudspeaker to sub levels are different
- the sub is still 30-40% too loud. need to update this with default param or CLI config. possibly scaled based on DBAP focus param?
