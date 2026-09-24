#include "preview_session.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  photo_editor::desktop::PreviewSession session;
  const auto cancellation_identity = session.latestGeneration();
  if (session.generation() != 0 || cancellation_identity->load() != 0 ||
      !session.canPublish(0, false) || !session.canPublish(0, true)) {
    return fail("a new preview session must expose one current zero generation");
  }

  const auto first_generation = session.advanceGeneration();
  if (first_generation != 1 || cancellation_identity->load() != 1 ||
      !session.isCurrent(first_generation) || session.isCurrent(0)) {
    return fail("advancing a session must invalidate prior asynchronous work");
  }

  session.markPublished(first_generation, false);
  session.markPublished(first_generation, true);
  if (!session.canPublish(first_generation, false) ||
      !session.canPublish(first_generation, true)) {
    return fail("the current generation must remain publishable by both stages");
  }

  const auto second_generation = session.advanceGeneration();
  session.markPublished(first_generation, false);
  if (session.canPublish(first_generation, false) ||
      !session.canPublish(second_generation, false) ||
      cancellation_identity->load() != second_generation) {
    return fail("stale completion must not alter current publication eligibility");
  }

  if (session.advancePublicationRevision() != 1 ||
      session.advancePublicationRevision() != 2 ||
      session.publicationRevision() != 2) {
    return fail("published preview revisions must be monotonic and session-owned");
  }

  return EXIT_SUCCESS;
}
