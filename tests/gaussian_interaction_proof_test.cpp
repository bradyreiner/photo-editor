#include "gaussian_interaction_proof.h"
#include <iostream>

int main() {
  using photo_editor::desktop::gaussian_interaction_proof;
  QVariantList samples;
  for (int i = 1; i <= 60; ++i)
    samples.append(QVariantMap{{"elapsedMs", i * 16}, {"gapMs", 16}, {"windowActive", true},
        {"windowExposed", true}, {"applicationState", static_cast<int>(Qt::ApplicationActive)}});
  if (!gaussian_interaction_proof(samples).value("passed").toBool()) return 1;
  for (const auto key : {"windowActive", "windowExposed", "applicationState"}) {
    auto invalid = samples;
    auto value = invalid[30].toMap(); value.remove(key); invalid[30] = value;
    const auto proof = gaussian_interaction_proof(invalid);
    if (proof.value("eligible").toBool() || proof.value("passed").toBool() ||
        proof.value("state") != "unverified-window-inactive") return 1;
  }
  auto short_run = samples; short_run.removeLast();
  if (gaussian_interaction_proof(short_run).value("state") != "incomplete") return 1;
  auto inconsistent = samples;
  auto last = inconsistent.last().toMap(); last["elapsedMs"] = 1; inconsistent.last() = last;
  if (gaussian_interaction_proof(inconsistent).value("eligible").toBool()) return 1;
  auto slow = samples;
  last = slow.last().toMap(); last["elapsedMs"] = 59 * 16 + 1000; last["gapMs"] = 1000; slow.last() = last;
  const auto over_budget = gaussian_interaction_proof(slow);
  if (!over_budget.value("eligible").toBool() || over_budget.value("passed").toBool() ||
      over_budget.value("state") != "over-budget") return 1;
  if (gaussian_interaction_proof({}).value("passed").toBool()) return 1;
  std::cout << "Foreground eligibility, trace integrity and interaction budget tests pass\n";
  return 0;
}
