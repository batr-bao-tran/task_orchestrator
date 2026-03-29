# Runtime Scheduling Strategy Benchmark Report

Seed: `20260329`. Iterations per scenario: `10`.

## Criteria verified

- `mean_latency_ns` / `p95_latency_ns`: planning responsiveness and latency tail behavior.
- `completion_ratio` and `mean_makespan`: throughput and runtime completion quality.
- `deadline_miss_rate` and `mean_tardiness`: deadline resilience during disruption.
- `constraint_violation_rate`: share of dispatched assignments that violated runtime feasibility checks.
- `mean_assignment_churn` and `mean_replans`: plan stability under replanning pressure.
- `mean_utilization`: actor busy-capacity divided by total declared actor capacity in dynamic scenarios.
- `preferred_actor_hit_ratio`, `mean_travel_distance`, and `mean_execution_cost`: affinity, locality, and cost quality under runtime replanning.

## Strategy scenarios

| Scenario | Goal | Chaotic | Strategy | Mean latency (ns) | P95 latency (ns) | Completion ratio | Deadline miss rate | Constraint violation rate | Mean tardiness | Mean makespan | Utilization | Preferred actor hit ratio | Mean travel distance | Mean execution cost | Assignment churn | Replans | OK |
|---|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
|throughput_release_contention_responsiveness_balanced_release_waves|throughput|no|EDF|9764.76|20597.00|1.00|0.00|0.00|0.00|15.30|0.85|0.00|0.00|0.00|0.42|6.90|yes|
|throughput_release_contention_responsiveness_balanced_release_waves|throughput|no|FIFO|9082.84|18279.00|1.00|0.00|0.00|0.00|15.00|0.86|0.00|0.00|0.00|0.42|7.00|yes|
|throughput_release_contention_responsiveness_balanced_release_waves|throughput|no|SJF|8582.75|18687.00|1.00|0.00|0.00|0.00|16.00|0.81|0.00|0.00|0.00|0.41|7.30|yes|
|throughput_release_contention_responsiveness_balanced_release_waves|throughput|no|PriorityOnly|85507.78|17910.00|1.00|0.00|0.00|0.00|15.30|0.85|0.00|0.00|0.00|0.42|6.90|yes|
|throughput_release_contention_responsiveness_chaotic_release_storm|throughput|yes|EDF|69939.28|39149.00|1.00|0.00|0.00|0.00|17.70|0.74|0.00|0.00|0.00|0.40|10.50|yes|
|throughput_release_contention_responsiveness_chaotic_release_storm|throughput|yes|FIFO|15990.10|35972.00|1.00|0.00|0.00|0.00|17.80|0.74|0.00|0.00|0.00|0.45|10.30|yes|
|throughput_release_contention_responsiveness_chaotic_release_storm|throughput|yes|SJF|104727.29|38167.00|1.00|0.00|0.00|0.00|18.90|0.69|0.00|0.00|0.00|0.47|10.30|yes|
|throughput_release_contention_responsiveness_chaotic_release_storm|throughput|yes|PriorityOnly|69447.24|35218.00|1.00|0.00|0.00|0.00|17.90|0.73|0.00|0.00|0.00|0.43|10.30|yes|
|deadline_resilience_with_actor_flapping_moderate_gap|deadlines|yes|EDF|5728.47|11868.00|1.00|0.00|0.00|0.00|19.20|0.77|0.00|0.00|0.00|0.54|6.80|yes|
|deadline_resilience_with_actor_flapping_moderate_gap|deadlines|yes|FIFO|44904.56|11779.00|1.00|0.00|0.00|0.00|19.20|0.77|0.00|0.00|0.00|0.54|6.80|yes|
|deadline_resilience_with_actor_flapping_moderate_gap|deadlines|yes|SJF|14502.96|13103.00|0.97|0.00|0.00|0.00|19.80|0.71|0.00|0.00|0.00|0.51|7.00|yes|
|deadline_resilience_with_actor_flapping_moderate_gap|deadlines|yes|PriorityOnly|6172.21|11202.00|1.00|0.00|0.00|0.00|19.20|0.77|0.00|0.00|0.00|0.54|6.80|yes|
|deadline_resilience_with_actor_flapping_severe_flapping_gap|deadlines|yes|EDF|37773.02|18150.00|1.00|0.00|0.00|0.00|27.30|0.74|0.00|0.00|0.00|0.50|8.80|yes|
|deadline_resilience_with_actor_flapping_severe_flapping_gap|deadlines|yes|FIFO|8688.60|17117.00|1.00|0.00|0.00|0.00|27.30|0.74|0.00|0.00|0.00|0.50|8.80|yes|
|deadline_resilience_with_actor_flapping_severe_flapping_gap|deadlines|yes|SJF|46705.20|18916.00|0.86|0.00|0.00|0.00|24.30|0.68|0.00|0.00|0.00|0.59|7.10|yes|
|deadline_resilience_with_actor_flapping_severe_flapping_gap|deadlines|yes|PriorityOnly|7700.40|15616.00|1.00|0.00|0.00|0.00|27.30|0.74|0.00|0.00|0.00|0.50|8.80|yes|
|replanning_stability_under_resumable_failures_dual_failure_wave|stability|yes|EDF|6173.06|13069.00|1.00|0.00|0.00|0.00|17.60|0.70|0.72|0.00|0.00|0.66|8.00|yes|
|replanning_stability_under_resumable_failures_dual_failure_wave|stability|yes|FIFO|6153.88|12960.00|1.00|0.00|0.00|0.00|17.60|0.70|0.72|0.00|0.00|0.66|8.00|yes|
|replanning_stability_under_resumable_failures_dual_failure_wave|stability|yes|SJF|41724.00|15129.00|1.00|0.00|0.00|0.00|19.30|0.72|0.54|0.00|0.00|0.65|7.70|yes|
|replanning_stability_under_resumable_failures_dual_failure_wave|stability|yes|PriorityOnly|6680.73|13978.00|1.00|0.00|0.00|0.00|17.60|0.70|0.72|0.00|0.00|0.66|8.00|yes|
|replanning_stability_under_resumable_failures_cascading_failure_wave|stability|yes|EDF|6360.03|13288.00|1.00|0.00|0.00|0.00|18.30|0.63|1.00|0.00|0.00|0.74|10.10|yes|
|replanning_stability_under_resumable_failures_cascading_failure_wave|stability|yes|FIFO|7305.60|14909.00|1.00|0.00|0.00|0.00|18.30|0.63|1.00|0.00|0.00|0.74|10.10|yes|
|replanning_stability_under_resumable_failures_cascading_failure_wave|stability|yes|SJF|6644.93|15171.00|1.00|0.00|0.00|0.00|17.50|0.67|0.77|0.00|0.00|0.74|9.10|yes|
|replanning_stability_under_resumable_failures_cascading_failure_wave|stability|yes|PriorityOnly|6347.78|13245.00|1.00|0.00|0.00|0.00|18.30|0.63|1.00|0.00|0.00|0.74|10.10|yes|
|dependency_flow_stability_under_multiphase_disruption_temporary_capacity_loss|stability|yes|EDF|3120.35|5760.00|1.00|0.00|0.00|0.00|19.50|0.54|0.00|0.00|0.00|0.70|7.00|yes|
|dependency_flow_stability_under_multiphase_disruption_temporary_capacity_loss|stability|yes|FIFO|2434.19|4410.00|1.00|0.00|0.00|0.00|19.50|0.54|0.00|0.00|0.00|0.70|7.00|yes|
|dependency_flow_stability_under_multiphase_disruption_temporary_capacity_loss|stability|yes|SJF|2795.55|4791.00|1.00|0.00|0.00|0.00|19.50|0.54|0.00|0.00|0.00|0.70|7.00|yes|
|dependency_flow_stability_under_multiphase_disruption_temporary_capacity_loss|stability|yes|PriorityOnly|2554.85|4586.00|1.00|0.00|0.00|0.00|19.50|0.54|0.00|0.00|0.00|0.70|7.00|yes|
|dependency_flow_stability_under_multiphase_disruption_cascading_multiphase_outage|stability|yes|EDF|2437.87|4477.00|1.00|0.00|0.00|0.00|20.90|0.50|0.00|0.00|0.00|1.00|7.90|yes|
|dependency_flow_stability_under_multiphase_disruption_cascading_multiphase_outage|stability|yes|FIFO|2348.70|4120.00|1.00|0.00|0.00|0.00|20.90|0.50|0.00|0.00|0.00|1.00|7.90|yes|
|dependency_flow_stability_under_multiphase_disruption_cascading_multiphase_outage|stability|yes|SJF|2460.27|4558.00|1.00|0.00|0.00|0.00|21.40|0.49|0.00|0.00|0.00|1.00|7.90|yes|
|dependency_flow_stability_under_multiphase_disruption_cascading_multiphase_outage|stability|yes|PriorityOnly|2863.64|4932.00|1.00|0.00|0.00|0.00|20.90|0.50|0.00|0.00|0.00|1.00|7.90|yes|
|capability_fragmentation_shift_gap_resilience_capability_fragmentation_shift_gap|stability|yes|EDF|3497.78|5768.00|1.00|0.00|0.00|0.00|12.10|0.27|0.50|2.50|3.90|0.83|3.10|yes|
|capability_fragmentation_shift_gap_resilience_capability_fragmentation_shift_gap|stability|yes|FIFO|3436.54|5967.00|1.00|0.00|0.00|0.00|12.10|0.27|0.50|2.50|3.90|0.83|3.10|yes|
|capability_fragmentation_shift_gap_resilience_capability_fragmentation_shift_gap|stability|yes|SJF|3658.68|6285.00|1.00|0.00|0.00|0.00|12.10|0.27|0.50|2.50|3.90|0.83|3.10|yes|
|capability_fragmentation_shift_gap_resilience_capability_fragmentation_shift_gap|stability|yes|PriorityOnly|3183.90|5241.00|1.00|0.00|0.00|0.00|12.10|0.27|0.50|2.50|3.90|0.83|3.10|yes|
|capability_fragmentation_shift_gap_resilience_severe_fragmentation_shift_gap|stability|yes|EDF|2868.33|4619.00|1.00|0.00|0.00|0.00|20.10|0.21|0.75|2.00|4.54|0.58|6.00|yes|
|capability_fragmentation_shift_gap_resilience_severe_fragmentation_shift_gap|stability|yes|FIFO|2795.71|4491.00|1.00|0.00|0.00|0.00|20.10|0.21|0.75|2.00|4.54|0.58|6.00|yes|
|capability_fragmentation_shift_gap_resilience_severe_fragmentation_shift_gap|stability|yes|SJF|47150.70|6084.00|1.00|0.00|0.00|0.00|24.60|0.17|0.53|2.67|4.99|0.66|6.00|yes|
|capability_fragmentation_shift_gap_resilience_severe_fragmentation_shift_gap|stability|yes|PriorityOnly|3306.81|5238.00|1.00|0.00|0.00|0.00|20.10|0.21|0.75|2.00|4.54|0.58|6.00|yes|

## Strategy recommendations

- `throughput_release_contention_responsiveness_balanced_release_waves`: **FIFO** best matched the `throughput` criterion in this run. Verifies how quickly each strategy reacts to rolling releases while preserving throughput and makespan quality under contention.
- `throughput_release_contention_responsiveness_chaotic_release_storm`: **EDF** best matched the `throughput` criterion in this run. Verifies how quickly each strategy reacts to rolling releases while preserving throughput and makespan quality under contention.
- `deadline_resilience_with_actor_flapping_moderate_gap`: **EDF** best matched the `deadlines` criterion in this run. Verifies deadline protection when capacity flaps during a deadline-heavy burst.
- `deadline_resilience_with_actor_flapping_severe_flapping_gap`: **EDF** best matched the `deadlines` criterion in this run. Verifies deadline protection when capacity flaps during a deadline-heavy burst.
- `replanning_stability_under_resumable_failures_dual_failure_wave`: **SJF** best matched the `stability` criterion in this run. Verifies replanning stability and churn when assigned work repeatedly fails and re-enters the ready queue.
- `replanning_stability_under_resumable_failures_cascading_failure_wave`: **SJF** best matched the `stability` criterion in this run. Verifies replanning stability and churn when assigned work repeatedly fails and re-enters the ready queue.
- `dependency_flow_stability_under_multiphase_disruption_temporary_capacity_loss`: **EDF** best matched the `stability` criterion in this run. Verifies whether strategy choices remain stable when dependency-gated work and temporary capacity loss interact across phases.
- `dependency_flow_stability_under_multiphase_disruption_cascading_multiphase_outage`: **EDF** best matched the `stability` criterion in this run. Verifies whether strategy choices remain stable when dependency-gated work and temporary capacity loss interact across phases.
- `capability_fragmentation_shift_gap_resilience_capability_fragmentation_shift_gap`: **EDF** best matched the `stability` criterion in this run. Verifies plan stability when capabilities are split across actors with staggered availability windows.
- `capability_fragmentation_shift_gap_resilience_severe_fragmentation_shift_gap`: **EDF** best matched the `stability` criterion in this run. Verifies plan stability when capabilities are split across actors with staggered availability windows.
