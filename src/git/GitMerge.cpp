#include "GitMerge.h"

#include <GitBase.h>
#include <GitRepoLoader.h>
#include <QLogger.h>

using namespace QLogger;

GitMerge::GitMerge(const QSharedPointer<GitBase> &gitBase, QSharedPointer<RevisionsCache> cache)
   : mGitBase(gitBase)
   , mCache(cache)
{
}

GitExecResult GitMerge::merge(const QString &into, QStringList sources)
{
   QLog_Debug("Git", QString("Executing merge: {%1} into {%2}").arg(sources.join(","), into));

   const auto retCheckout = mGitBase->run(QString("git checkout -q %1").arg(into));

   if (!retCheckout.success)
      return retCheckout;

   const auto retMerge = mGitBase->run(QString("git merge ") + sources.join(" "));

   if (retMerge.success)
   {
      QScopedPointer<GitRepoLoader> gitLoader(new GitRepoLoader(mGitBase, mCache));
      gitLoader->updateWipRevision();
   }

   return retMerge;
}

GitExecResult GitMerge::abortMerge() const
{
   QLog_Debug("Git", QString("Merge aborted"));

   return mGitBase->run("git merge --abort");
}

GitExecResult GitMerge::applyMerge() const
{
   QLog_Debug("Git", QString("Merge commit"));

   return mGitBase->run("git commit --no-edit");
}
