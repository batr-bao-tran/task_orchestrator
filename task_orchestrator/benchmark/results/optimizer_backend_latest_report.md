# Optimization Backend Comparison Benchmark Report

Seed: `20260328`. Iterations per scenario: `10`.

## Criteria verified

- `mean_latency_ns` / `p95_latency_ns`: solve latency and latency tail behavior.
- `fulfillment_ratio` and `mean_makespan`: throughput and schedule-completion quality.
- `deadline_miss_rate` and `mean_tardiness`: deadline-feasibility behavior under pressure.
- `preferred_actor_hit_ratio`, `mean_travel_distance`, and `mean_execution_cost`: cost and preference tradeoff quality.

## Backend scenarios

| Scenario | Goal | Backend | Mean latency (ns) | P95 latency (ns) | Fulfillment ratio | Deadline miss rate | Mean tardiness | Mean makespan | Preferred hit ratio | Mean travel distance | Mean execution cost | OK | Notes |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
|throughput_maximization_parallel_fulfillment_balanced_release_waves|throughput|commercial_mip[SCIP]|38888849.50|34672964.00|1.00|0.00|0.00|18.00|0.00|0.00|22.50|yes||
|throughput_maximization_parallel_fulfillment_balanced_release_waves|throughput|indexed_branch_and_bound|1189952.00|1218545.00|1.00|0.00|0.00|8.70|0.00|0.00|42.10|yes||
|throughput_maximization_parallel_fulfillment_balanced_release_waves|throughput|ortools_cp_sat|33811136.40|48007360.00|1.00|0.00|0.00|16.80|0.00|0.00|22.50|yes||
|throughput_maximization_parallel_fulfillment_fragmented_shift_windows|throughput|commercial_mip[SCIP]|87301539.10|138433594.00|1.00|0.00|0.00|27.90|0.00|0.00|26.80|yes||
|throughput_maximization_parallel_fulfillment_fragmented_shift_windows|throughput|indexed_branch_and_bound|139847269.00|142991468.00|1.00|0.00|0.00|14.30|0.00|0.00|49.20|yes||
|throughput_maximization_parallel_fulfillment_fragmented_shift_windows|throughput|ortools_cp_sat|106109309.40|250805974.00|1.00|0.00|0.00|24.50|0.00|0.00|26.80|yes||
|deadline_feasibility_under_precedence_pressure_moderate_slack|deadlines|commercial_mip[SCIP]|6000190.90|6424013.00|0.00|0.00|0.00|0.00|0.00|0.00|0.00|no|MIP solve failed with engine SCIP and status code 2.|
|deadline_feasibility_under_precedence_pressure_moderate_slack|deadlines|indexed_branch_and_bound|17133.40|28291.00|0.00|0.00|0.00|0.00|0.00|0.00|0.00|no|No optimization result could be produced.|
|deadline_feasibility_under_precedence_pressure_moderate_slack|deadlines|ortools_cp_sat|1140827.00|1185337.00|0.00|0.00|0.00|0.00|0.00|0.00|0.00|no|CP-SAT solve ended with status INFEASIBLE.|
|deadline_feasibility_under_precedence_pressure_severe_slack_shock|deadlines|commercial_mip[SCIP]|5546370.50|6055763.00|0.00|0.00|0.00|0.00|0.00|0.00|0.00|no|MIP solve failed with engine SCIP and status code 2.|
|deadline_feasibility_under_precedence_pressure_severe_slack_shock|deadlines|indexed_branch_and_bound|8091.70|10592.00|0.00|0.00|0.00|0.00|0.00|0.00|0.00|no|No optimization result could be produced.|
|deadline_feasibility_under_precedence_pressure_severe_slack_shock|deadlines|ortools_cp_sat|1090291.10|1113597.00|0.00|0.00|0.00|0.00|0.00|0.00|0.00|no|CP-SAT solve ended with status INFEASIBLE.|
|cost_efficiency_with_preferred_actor_tradeoffs_balanced_tradeoff_surface|cost|commercial_mip[SCIP]|15178885.80|17059816.00|1.00|0.00|0.00|24.30|0.40|30.00|24.30|yes||
|cost_efficiency_with_preferred_actor_tradeoffs_balanced_tradeoff_surface|cost|indexed_branch_and_bound|65225.30|69601.00|1.00|0.00|0.00|13.40|0.70|22.70|75.10|yes||
|cost_efficiency_with_preferred_actor_tradeoffs_balanced_tradeoff_surface|cost|ortools_cp_sat|23840818.10|27742027.00|1.00|0.00|0.00|24.30|0.40|30.00|24.30|yes||
|cost_efficiency_with_preferred_actor_tradeoffs_preference_distance_shock|cost|commercial_mip[SCIP]|14977345.20|16926928.00|1.00|0.00|0.00|24.30|0.40|45.00|24.30|yes||
|cost_efficiency_with_preferred_actor_tradeoffs_preference_distance_shock|cost|indexed_branch_and_bound|62601.50|66347.00|1.00|0.00|0.00|13.40|0.70|32.30|75.10|yes||
|cost_efficiency_with_preferred_actor_tradeoffs_preference_distance_shock|cost|ortools_cp_sat|23527657.70|26647325.00|1.00|0.00|0.00|24.30|0.40|45.00|24.30|yes||
|overload_resilience_with_partial_fulfillment_moderate_overload|throughput|commercial_mip[SCIP]|8337648.90|8854458.00|0.57|0.00|0.00|18.00|0.00|0.00|27.20|yes||
|overload_resilience_with_partial_fulfillment_moderate_overload|throughput|indexed_branch_and_bound|33016.90|39177.00|0.57|0.00|0.00|14.40|0.00|0.00|27.20|yes||
|overload_resilience_with_partial_fulfillment_moderate_overload|throughput|ortools_cp_sat|6763752.70|7587981.00|0.57|0.00|0.00|14.40|0.00|0.00|27.20|yes||
|overload_resilience_with_partial_fulfillment_hard_overload|throughput|commercial_mip[SCIP]|15267932.70|19975807.00|0.56|0.00|0.00|19.80|0.00|0.00|33.70|yes||
|overload_resilience_with_partial_fulfillment_hard_overload|throughput|indexed_branch_and_bound|83762.60|92869.00|0.56|0.00|0.00|18.80|0.00|0.00|33.70|yes||
|overload_resilience_with_partial_fulfillment_hard_overload|throughput|ortools_cp_sat|11134148.70|13319929.00|0.56|0.00|0.00|18.40|0.00|0.00|33.70|yes||
|capability_fragmentation_under_shift_gap_pressure_staggered_shift_caps|throughput|commercial_mip[SCIP]|51292073.60|57460514.00|1.00|0.00|0.00|15.70|1.00|6.00|26.55|yes||
|capability_fragmentation_under_shift_gap_pressure_staggered_shift_caps|throughput|indexed_branch_and_bound|89618.10|93400.00|1.00|0.00|0.00|15.90|1.00|6.00|26.55|yes||
|capability_fragmentation_under_shift_gap_pressure_staggered_shift_caps|throughput|ortools_cp_sat|18980850.40|23567811.00|1.00|0.00|0.00|14.90|1.00|6.00|26.55|yes||
|capability_fragmentation_under_shift_gap_pressure_severe_capability_fragmentation|throughput|commercial_mip[SCIP]|12395262.50|14055174.00|0.67|0.00|0.00|13.50|1.00|4.00|17.90|yes||
|capability_fragmentation_under_shift_gap_pressure_severe_capability_fragmentation|throughput|indexed_branch_and_bound|48937.80|52936.00|0.67|0.00|0.00|13.40|1.00|4.00|17.90|yes||
|capability_fragmentation_under_shift_gap_pressure_severe_capability_fragmentation|throughput|ortools_cp_sat|8256854.10|8806309.00|0.67|0.00|0.00|13.40|1.00|4.00|17.90|yes||

## Backend recommendations

- `throughput_maximization_parallel_fulfillment_balanced_release_waves`: **indexed_branch_and_bound** best matched the `throughput` criterion in this run. Verifies whether the optimizer maximizes fulfillment speed and makespan quality under release waves and actor-window contention.
- `throughput_maximization_parallel_fulfillment_fragmented_shift_windows`: **indexed_branch_and_bound** best matched the `throughput` criterion in this run. Verifies whether the optimizer maximizes fulfillment speed and makespan quality under release waves and actor-window contention.
- `deadline_feasibility_under_precedence_pressure_moderate_slack`: **commercial_mip[SCIP]** best matched the `deadlines` criterion in this run. Verifies deadline feasibility, precedence handling, and preferred-actor quality when slack is compressed by chained work.
- `deadline_feasibility_under_precedence_pressure_severe_slack_shock`: **commercial_mip[SCIP]** best matched the `deadlines` criterion in this run. Verifies deadline feasibility, precedence handling, and preferred-actor quality when slack is compressed by chained work.
- `cost_efficiency_with_preferred_actor_tradeoffs_balanced_tradeoff_surface`: **indexed_branch_and_bound** best matched the `cost` criterion in this run. Verifies whether the optimizer balances preferred actors, travel distance, and execution cost when the objective surface is intentionally conflicted.
- `cost_efficiency_with_preferred_actor_tradeoffs_preference_distance_shock`: **indexed_branch_and_bound** best matched the `cost` criterion in this run. Verifies whether the optimizer balances preferred actors, travel distance, and execution cost when the objective surface is intentionally conflicted.
- `overload_resilience_with_partial_fulfillment_moderate_overload`: **indexed_branch_and_bound** best matched the `throughput` criterion in this run. Verifies graceful degradation, mandatory-task protection, and partial-plan quality when demand significantly exceeds available capacity.
- `overload_resilience_with_partial_fulfillment_hard_overload`: **ortools_cp_sat** best matched the `throughput` criterion in this run. Verifies graceful degradation, mandatory-task protection, and partial-plan quality when demand significantly exceeds available capacity.
- `capability_fragmentation_under_shift_gap_pressure_staggered_shift_caps`: **ortools_cp_sat** best matched the `throughput` criterion in this run. Verifies whether the optimizer can recover throughput when capabilities are split across staggered shifts and dependency chains.
- `capability_fragmentation_under_shift_gap_pressure_severe_capability_fragmentation`: **indexed_branch_and_bound** best matched the `throughput` criterion in this run. Verifies whether the optimizer can recover throughput when capabilities are split across staggered shifts and dependency chains.
