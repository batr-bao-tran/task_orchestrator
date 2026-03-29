# Runtime Scheduling Strategy Benchmark Report

Seed: `20260328`. Iterations per scenario: `10`.

## Criteria verified

- `mean_latency_ns` / `p95_latency_ns`: planning responsiveness and latency tail behavior.
- `completion_ratio` and `mean_makespan`: throughput and runtime completion quality.
- `deadline_miss_rate` and `mean_tardiness`: deadline resilience during disruption.
- `mean_assignment_churn` and `mean_replans`: plan stability under replanning pressure.
- `mean_utilization`: actor busy-capacity divided by total declared actor capacity in dynamic scenarios.

## Strategy scenarios

| Scenario | Goal | Chaotic | Strategy | Mean latency (ns) | P95 latency (ns) | Completion ratio | Deadline miss rate | Mean tardiness | Mean makespan | Utilization | Assignment churn | Replans | OK |
|---|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
|throughput_release_contention_responsiveness_balanced_release_waves|throughput|no|EDF|2867.16|4117.00|1.00|0.00|0.00|17.80|0.77|0.97|7.50|yes|
|throughput_release_contention_responsiveness_balanced_release_waves|throughput|no|FIFO|2540.33|3736.00|1.00|0.00|0.00|15.70|0.88|0.99|7.30|yes|
|throughput_release_contention_responsiveness_balanced_release_waves|throughput|no|SJF|2699.47|3886.00|1.00|0.00|0.00|19.50|0.70|0.96|6.90|yes|
|throughput_release_contention_responsiveness_balanced_release_waves|throughput|no|PriorityOnly|2582.45|3485.00|1.00|0.00|0.00|17.80|0.77|0.97|7.50|yes|
|throughput_release_contention_responsiveness_chaotic_release_storm|throughput|yes|EDF|4613.35|7341.00|1.00|0.00|0.00|20.10|0.68|0.97|11.00|yes|
|throughput_release_contention_responsiveness_chaotic_release_storm|throughput|yes|FIFO|4532.75|7313.00|1.00|0.00|0.00|19.20|0.72|0.99|10.40|yes|
|throughput_release_contention_responsiveness_chaotic_release_storm|throughput|yes|SJF|4451.81|7017.00|1.00|0.00|0.00|20.30|0.68|0.97|10.60|yes|
|throughput_release_contention_responsiveness_chaotic_release_storm|throughput|yes|PriorityOnly|4250.81|6565.00|1.00|0.00|0.00|20.50|0.67|0.97|10.50|yes|
|deadline_resilience_with_actor_flapping_moderate_gap|deadlines|yes|EDF|1886.17|2852.00|1.00|0.00|0.00|20.00|0.77|1.00|7.30|yes|
|deadline_resilience_with_actor_flapping_moderate_gap|deadlines|yes|FIFO|1791.90|2564.00|1.00|0.00|0.00|20.00|0.77|1.00|7.30|yes|
|deadline_resilience_with_actor_flapping_moderate_gap|deadlines|yes|SJF|1956.30|2753.00|0.93|0.00|0.00|18.90|0.74|1.00|6.60|yes|
|deadline_resilience_with_actor_flapping_moderate_gap|deadlines|yes|PriorityOnly|1732.17|2404.00|1.00|0.00|0.00|20.00|0.77|1.00|7.30|yes|
|deadline_resilience_with_actor_flapping_severe_flapping_gap|deadlines|yes|EDF|2307.47|3096.00|0.99|0.00|0.00|26.60|0.76|1.00|9.30|yes|
|deadline_resilience_with_actor_flapping_severe_flapping_gap|deadlines|yes|FIFO|2336.37|3055.00|0.99|0.00|0.00|26.60|0.76|1.00|9.30|yes|
|deadline_resilience_with_actor_flapping_severe_flapping_gap|deadlines|yes|SJF|2628.69|3286.00|0.82|0.00|0.00|23.20|0.68|1.00|6.70|yes|
|deadline_resilience_with_actor_flapping_severe_flapping_gap|deadlines|yes|PriorityOnly|2528.58|3232.00|0.99|0.00|0.00|26.60|0.76|1.00|9.30|yes|
|replanning_stability_under_resumable_failures_dual_failure_wave|stability|yes|EDF|1602.25|2790.00|1.00|0.00|0.00|16.90|0.63|1.00|8.50|yes|
|replanning_stability_under_resumable_failures_dual_failure_wave|stability|yes|FIFO|1502.64|2524.00|1.00|0.00|0.00|16.90|0.63|1.00|8.50|yes|
|replanning_stability_under_resumable_failures_dual_failure_wave|stability|yes|SJF|1581.90|3013.00|1.00|0.00|0.00|16.70|0.68|1.00|7.40|yes|
|replanning_stability_under_resumable_failures_dual_failure_wave|stability|yes|PriorityOnly|1388.97|2402.00|1.00|0.00|0.00|16.90|0.63|1.00|8.50|yes|
|replanning_stability_under_resumable_failures_cascading_failure_wave|stability|yes|EDF|1493.58|2331.00|1.00|0.00|0.00|15.40|0.64|1.00|9.80|yes|
|replanning_stability_under_resumable_failures_cascading_failure_wave|stability|yes|FIFO|1495.48|2572.00|1.00|0.00|0.00|15.40|0.64|1.00|9.80|yes|
|replanning_stability_under_resumable_failures_cascading_failure_wave|stability|yes|SJF|1500.47|2808.00|1.00|0.00|0.00|13.30|0.77|1.00|8.30|yes|
|replanning_stability_under_resumable_failures_cascading_failure_wave|stability|yes|PriorityOnly|1452.89|2466.00|1.00|0.00|0.00|15.40|0.64|1.00|9.80|yes|
|dependency_flow_stability_under_multiphase_disruption_temporary_capacity_loss|stability|yes|EDF|941.08|1467.00|1.00|0.00|0.00|19.60|0.54|1.00|7.00|yes|
|dependency_flow_stability_under_multiphase_disruption_temporary_capacity_loss|stability|yes|FIFO|896.60|1326.00|1.00|0.00|0.00|19.60|0.54|1.00|7.00|yes|
|dependency_flow_stability_under_multiphase_disruption_temporary_capacity_loss|stability|yes|SJF|805.98|1235.00|1.00|0.00|0.00|19.60|0.54|0.97|7.00|yes|
|dependency_flow_stability_under_multiphase_disruption_temporary_capacity_loss|stability|yes|PriorityOnly|812.21|1218.00|1.00|0.00|0.00|19.60|0.54|1.00|7.00|yes|
|dependency_flow_stability_under_multiphase_disruption_cascading_multiphase_outage|stability|yes|EDF|809.12|1203.00|1.00|0.00|0.00|21.10|0.50|1.00|7.90|yes|
|dependency_flow_stability_under_multiphase_disruption_cascading_multiphase_outage|stability|yes|FIFO|832.15|1209.00|1.00|0.00|0.00|21.10|0.50|1.00|7.90|yes|
|dependency_flow_stability_under_multiphase_disruption_cascading_multiphase_outage|stability|yes|SJF|830.00|1228.00|1.00|0.00|0.00|21.60|0.49|1.00|7.90|yes|
|dependency_flow_stability_under_multiphase_disruption_cascading_multiphase_outage|stability|yes|PriorityOnly|816.04|1232.00|1.00|0.00|0.00|21.10|0.50|1.00|7.90|yes|
|capability_fragmentation_shift_gap_resilience_capability_fragmentation_shift_gap|stability|yes|EDF|1105.79|1680.00|1.00|0.00|0.00|12.20|0.44|1.00|3.20|yes|
|capability_fragmentation_shift_gap_resilience_capability_fragmentation_shift_gap|stability|yes|FIFO|1064.29|1499.00|1.00|0.00|0.00|12.20|0.44|1.00|3.20|yes|
|capability_fragmentation_shift_gap_resilience_capability_fragmentation_shift_gap|stability|yes|SJF|996.10|1538.00|1.00|0.00|0.00|12.20|0.44|1.00|3.20|yes|
|capability_fragmentation_shift_gap_resilience_capability_fragmentation_shift_gap|stability|yes|PriorityOnly|990.90|1395.00|1.00|0.00|0.00|12.20|0.44|1.00|3.20|yes|
|capability_fragmentation_shift_gap_resilience_severe_fragmentation_shift_gap|stability|yes|EDF|1081.00|1427.00|1.00|0.00|0.00|20.20|0.27|0.58|6.00|yes|
|capability_fragmentation_shift_gap_resilience_severe_fragmentation_shift_gap|stability|yes|FIFO|1047.67|1354.00|1.00|0.00|0.00|20.20|0.27|0.58|6.00|yes|
|capability_fragmentation_shift_gap_resilience_severe_fragmentation_shift_gap|stability|yes|SJF|1088.00|1384.00|1.00|0.00|0.00|24.20|0.22|0.78|6.00|yes|
|capability_fragmentation_shift_gap_resilience_severe_fragmentation_shift_gap|stability|yes|PriorityOnly|1012.63|1303.00|1.00|0.00|0.00|20.20|0.27|0.58|6.00|yes|

## Strategy recommendations

- `throughput_release_contention_responsiveness_balanced_release_waves`: **FIFO** best matched the `throughput` criterion in this run. Verifies how quickly each strategy reacts to rolling releases while preserving throughput and makespan quality under contention.
- `throughput_release_contention_responsiveness_chaotic_release_storm`: **FIFO** best matched the `throughput` criterion in this run. Verifies how quickly each strategy reacts to rolling releases while preserving throughput and makespan quality under contention.
- `deadline_resilience_with_actor_flapping_moderate_gap`: **EDF** best matched the `deadlines` criterion in this run. Verifies deadline protection when capacity flaps during a deadline-heavy burst.
- `deadline_resilience_with_actor_flapping_severe_flapping_gap`: **EDF** best matched the `deadlines` criterion in this run. Verifies deadline protection when capacity flaps during a deadline-heavy burst.
- `replanning_stability_under_resumable_failures_dual_failure_wave`: **SJF** best matched the `stability` criterion in this run. Verifies replanning stability and churn when assigned work repeatedly fails and re-enters the ready queue.
- `replanning_stability_under_resumable_failures_cascading_failure_wave`: **SJF** best matched the `stability` criterion in this run. Verifies replanning stability and churn when assigned work repeatedly fails and re-enters the ready queue.
- `dependency_flow_stability_under_multiphase_disruption_temporary_capacity_loss`: **SJF** best matched the `stability` criterion in this run. Verifies whether strategy choices remain stable when dependency-gated work and temporary capacity loss interact across phases.
- `dependency_flow_stability_under_multiphase_disruption_cascading_multiphase_outage`: **EDF** best matched the `stability` criterion in this run. Verifies whether strategy choices remain stable when dependency-gated work and temporary capacity loss interact across phases.
- `capability_fragmentation_shift_gap_resilience_capability_fragmentation_shift_gap`: **EDF** best matched the `stability` criterion in this run. Verifies plan stability when capabilities are split across actors with staggered availability windows.
- `capability_fragmentation_shift_gap_resilience_severe_fragmentation_shift_gap`: **EDF** best matched the `stability` criterion in this run. Verifies plan stability when capabilities are split across actors with staggered availability windows.
