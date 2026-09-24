# Nonblocking Gaussian reconstruction controller teardown

## Defect and change

`GaussianSceneController` waited for preparation on the GUI thread and owned a
GUI-thread `QProcess` whose shutdown could wait twice for one second. A held
preparation reproduced 506,491 microseconds of teardown; a backend that ignored
SIGTERM reproduced 1,007,528 microseconds. These are deliberately held fixtures,
not measurements of typical photo decoding.

Reconstruction and training now run through an owned worker. That worker creates,
reads, terminates, kills when necessary, and reaps its own child process. Its
inputs and cancellation token are captured by value, without a controller
pointer. Destroying the controller signals cancellation and does not wait for
preparation or the process. The worker gives a canceled backend one second to
terminate before killing it. Preparation retains its existing cooperative token.

The GUI polls a bounded 32,768-character log mailbox every 100ms and consumes the
final output before advancing stages. Explicit active flags remain set through
completion delivery, preventing a replacement operation from overlapping a
finished-but-not-yet-consumed worker. A cancellation token belongs to one
operation; there is no delayed GUI timer that could kill a later backend.

Interrupted projects retain their saved busy checkpoint and reopen as
`interrupted`; opening never resumes persisted commands. Manual cancellation
reaches `canceled` after worker completion. Existing original-file protection,
stage arguments, training schedule, artifact validation and read-only reopening
are unchanged.

## Verification

- New `gaussian_controller_lifetime_test`: held preparation destruction,
  SIGTERM-resistant child destruction and cancellation, actual child reaping,
  progress publication, rejection of premature restart/project switching,
  successful operation admission after cancellation, cancellation before launch,
  failed process start, final output delivery and bounded logs.
- Final native fixture results: preparation teardown **5 microseconds**, process
  teardown **17 microseconds**, manual cancel return **953 microseconds**. The
  declared fixture gate is 100ms. The GUI event loop continues for more than 300
  test iterations while each resistant child is stopped and reaped.
- All **11 Gaussian tests pass**, including reconstruction protocol, artifact
  integrity, viewport handoff, native Metal rendering and film export.
- Desktop app, controller tests and production pipeline driver build successfully.
- Isolated native app proof opens the existing 753,038-splat project, validates
  and renders it, captures the workspace, and shuts down. Receipt `ok: true`;
  screenshot inspected. The catalog-unavailable banner is intentional in this
  closed-port QA profile, not a user-catalog failure. No model weights are loaded.

Evidence:

```text
/private/tmp/gaussian-controller-lifetime-before-build.log
/private/tmp/gaussian-controller-lifetime-after-build.log
/private/tmp/gaussian-controller-lifetime-after-test.log
/private/tmp/gaussian-controller-lifetime-final-build.log
/private/tmp/gaussian-controller-lifetime-final-test.log
/private/tmp/gaussian-controller-lifetime-regressions.log
/private/tmp/gaussian-controller-ui-1bo04f/receipt.json
/private/tmp/gaussian-controller-ui-1bo04f/startup.png
```

The pre-fix timing values were observed directly in the test tool output. The
initial sandboxed run did not complete the manual signal test; the accepted
native runs above exercise child signaling and reaping with host permission.

## Boundaries

This is controller-local responsiveness, not an app-wide realtime or shutdown
latency guarantee. Qt's global thread pool can still drain work at process exit.
Custom capture preparers must own their captures and honor cancellation; a
noncooperative decoder cannot be forcibly interrupted here. Process termination
covers the directly owned backend, not an arbitrary descendant process tree or
an uninterruptible OS process. Status-file publication is still synchronous.

The smoke test reopens an existing trained scene; it does not repeat full real
COLMAP/Brush training. The protocol test exercises every stage through the new
worker. Reconstruction quality, full 4D capture, film fidelity, embedded model
provisioning and broader editor realtime remain separate unfinished work.
