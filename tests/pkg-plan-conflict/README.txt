pkg planner contradiction regression

Creates a deliberately impossible repository graph:

  a=2 requires b=2
  b=2 requires a=1

pkg plan must return failure and identify the selected-version contradiction.
The regression harness treats rc=1 as the expected result for this negative test.
