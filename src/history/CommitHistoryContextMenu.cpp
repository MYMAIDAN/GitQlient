#include "CommitHistoryContextMenu.h"

#include <GitLocal.h>
#include <GitPatches.h>
#include <GitBase.h>
#include <GitStashes.h>
#include <GitBranches.h>
#include <GitRemote.h>
#include <BranchDlg.h>
#include <TagDlg.h>
#include <CommitInfo.h>
#include <RevisionsCache.h>
#include <PullDlg.h>

#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QProcess>

#include <QLogger.h>

using namespace QLogger;

CommitHistoryContextMenu::CommitHistoryContextMenu(const QSharedPointer<RevisionsCache> &cache,
                                                   const QSharedPointer<GitBase> &git, const QStringList &shas,
                                                   QWidget *parent)
   : QMenu(parent)
   , mCache(cache)
   , mGit(git)
   , mShas(shas)
{
   setAttribute(Qt::WA_DeleteOnClose);

   if (shas.count() == 1)
      createIndividualShaMenu();
   else
      createMultipleShasMenu();
}

void CommitHistoryContextMenu::createIndividualShaMenu()
{
   if (mShas.count() == 1)
   {
      const auto sha = mShas.first();

      if (sha == CommitInfo::ZERO_SHA)
      {
         const auto stashAction = addAction("Push stash");
         connect(stashAction, &QAction::triggered, this, &CommitHistoryContextMenu::stashPush);

         const auto popAction = addAction("Pop stash");
         connect(popAction, &QAction::triggered, this, &CommitHistoryContextMenu::stashPop);
      }

      const auto commitAction = addAction("See diff");
      connect(commitAction, &QAction::triggered, this, [this]() { emit signalOpenDiff(mShas.first()); });

      if (sha != CommitInfo::ZERO_SHA)
      {
         const auto createBranchAction = addAction("Create branch here");
         connect(createBranchAction, &QAction::triggered, this, &CommitHistoryContextMenu::createBranch);

         const auto createTagAction = addAction("Create tag here");
         connect(createTagAction, &QAction::triggered, this, &CommitHistoryContextMenu::createTag);

         const auto exportAsPatchAction = addAction("Export as patch");
         connect(exportAsPatchAction, &QAction::triggered, this, &CommitHistoryContextMenu::exportAsPatch);

         addSeparator();

         const auto checkoutCommitAction = addAction("Checkout commit");
         connect(checkoutCommitAction, &QAction::triggered, this, &CommitHistoryContextMenu::checkoutCommit);

         addBranchActions(sha);

         QScopedPointer<GitBranches> git(new GitBranches(mGit));
         const auto ret = git->getLastCommitOfBranch(mGit->getCurrentBranch());

         if (ret.success)
         {
            const auto lastShaStr = ret.output.toString().remove('\n');

            if (lastShaStr == sha)
            {
               const auto amendCommitAction = addAction("Amend");
               connect(amendCommitAction, &QAction::triggered, this,
                       [this]() { emit signalAmendCommit(mShas.first()); });

               const auto applyPatchAction = addAction("Apply patch");
               connect(applyPatchAction, &QAction::triggered, this, &CommitHistoryContextMenu::applyPatch);

               const auto applyCommitAction = addAction("Apply commit");
               connect(applyCommitAction, &QAction::triggered, this, &CommitHistoryContextMenu::applyCommit);

               const auto pushAction = addAction("Push");
               connect(pushAction, &QAction::triggered, this, &CommitHistoryContextMenu::push);

               const auto pullAction = addAction("Pull");
               connect(pullAction, &QAction::triggered, this, &CommitHistoryContextMenu::pull);

               const auto fetchAction = addAction("Fetch");
               connect(fetchAction, &QAction::triggered, this, &CommitHistoryContextMenu::fetch);
            }
         }

         const auto copyShaAction = addAction("Copy SHA");
         connect(copyShaAction, &QAction::triggered, this,
                 [this]() { QApplication::clipboard()->setText(mShas.first()); });

         addSeparator();

         const auto resetSoftAction = addAction("Reset - Soft");
         connect(resetSoftAction, &QAction::triggered, this, &CommitHistoryContextMenu::resetSoft);

         const auto resetMixedAction = addAction("Reset - Mixed");
         connect(resetMixedAction, &QAction::triggered, this, &CommitHistoryContextMenu::resetMixed);

         const auto resetHardAction = addAction("Reset - Hard");
         connect(resetHardAction, &QAction::triggered, this, &CommitHistoryContextMenu::resetHard);
      }
   }
}

void CommitHistoryContextMenu::createMultipleShasMenu()
{
   if (mShas.count() == 2)
   {
      const auto diffAction = addAction("See diff");
      connect(diffAction, &QAction::triggered, this, [this]() { emit signalOpenCompareDiff(mShas); });
   }

   if (!mShas.contains(CommitInfo::ZERO_SHA))
   {
      const auto exportAsPatchAction = addAction("Export as patch");
      connect(exportAsPatchAction, &QAction::triggered, this, &CommitHistoryContextMenu::exportAsPatch);

      const auto copyShaAction = addAction("Copy all SHA");
      connect(copyShaAction, &QAction::triggered, this,
              [this]() { QApplication::clipboard()->setText(mShas.join(',')); });
   }
   else
      QLog_Warning("UI", "WIP selected as part of a series of SHAs");
}

void CommitHistoryContextMenu::stashPush()
{
   QScopedPointer<GitStashes> git(new GitStashes(mGit));
   const auto ret = git->stash();

   if (ret.success)
      emit signalRepositoryUpdated();
}

void CommitHistoryContextMenu::stashPop()
{
   QScopedPointer<GitStashes> git(new GitStashes(mGit));
   const auto ret = git->pop();

   if (ret.success)
      emit signalRepositoryUpdated();
}

void CommitHistoryContextMenu::createBranch()
{
   BranchDlg dlg({ mShas.first(), BranchDlgMode::CREATE_FROM_COMMIT, mGit });
   const auto ret = dlg.exec();

   if (ret == QDialog::Accepted)
      emit signalRepositoryUpdated();
}

void CommitHistoryContextMenu::createTag()
{
   TagDlg dlg(QSharedPointer<GitBase>::create(mGit->getWorkingDir()), mShas.first());
   const auto ret = dlg.exec();

   if (ret == QDialog::Accepted)
      emit signalRepositoryUpdated();
}

void CommitHistoryContextMenu::exportAsPatch()
{
   QScopedPointer<GitPatches> git(new GitPatches(mGit));
   const auto ret = git->exportPatch(mShas);

   if (ret.success)
   {
      const auto action = QMessageBox::information(
          this, tr("Patch generated"),
          tr("<p>The patch has been generated!</p>"
             "<p><b>Commit:</b></p><p>%1</p>"
             "<p><b>Destination:</b> %2</p>"
             "<p><b>File names:</b></p><p>%3</p>")
              .arg(mShas.join("<br>"), mGit->getWorkingDir(), ret.output.toStringList().join("<br>")),
          QMessageBox::Ok, QMessageBox::Open);

      if (action == QMessageBox::Open)
      {
         QString fileBrowser;

#ifdef Q_OS_LINUX
         fileBrowser.append("xdg-open");
#elif defined(Q_OS_WIN)
         fileBrowser.append("explorer.exe");
#endif

         QProcess::startDetached(QString("%1 %2").arg(fileBrowser, mGit->getWorkingDir()));
      }
   }
}

void CommitHistoryContextMenu::checkoutBranch()
{
   const auto action = qobject_cast<QAction *>(sender());
   auto isLocal = action->data().toBool();
   auto branchName = action->text();
   branchName.remove("origin/");

   QScopedPointer<GitBranches> git(new GitBranches(mGit));
   const auto ret = isLocal ? git->checkoutLocalBranch(branchName) : git->checkoutRemoteBranch(branchName);
   const auto output = ret.output.toString();

   if (ret.success)
   {
      QRegExp rx("by \\d+ commits");
      rx.indexIn(ret.output.toString());
      auto value = rx.capturedTexts().constFirst().split(" ");

      if (value.count() == 3 && output.contains("your branch is behind", Qt::CaseInsensitive))
      {
         const auto commits = value.at(1).toUInt();
         (void)commits;

         PullDlg pull(mGit, output.split('\n').first());

         connect(&pull, &PullDlg::signalRepositoryUpdated, this, &CommitHistoryContextMenu::signalRepositoryUpdated);
         connect(&pull, &PullDlg::signalPullConflict, this, &CommitHistoryContextMenu::signalPullConflict);
      }

      emit signalRepositoryUpdated();
   }
   else
      QMessageBox::critical(this, tr("Checkout error"), ret.output.toString());
}

void CommitHistoryContextMenu::checkoutCommit()
{
   const auto sha = mShas.first();
   QLog_Info("UI", QString("Checking out the commit {%1}").arg(sha));

   QScopedPointer<GitLocal> git(new GitLocal(mGit));
   const auto ret = git->checkoutCommit(sha);

   if (ret.success)
      emit signalRepositoryUpdated();
   else
      QMessageBox::critical(this, tr("Checkout error"), ret.output.toString());
}

void CommitHistoryContextMenu::cherryPickCommit()
{
   QScopedPointer<GitLocal> git(new GitLocal(mGit));
   const auto ret = git->cherryPickCommit(mShas.first());

   if (ret.success)
      emit signalRepositoryUpdated();
   else
   {
      const auto errorMsg = ret.output.toString();

      if (errorMsg.contains("error: could not apply", Qt::CaseInsensitive)
          && errorMsg.contains("causing a conflict", Qt::CaseInsensitive))
      {
         emit signalCherryPickConflict();
      }
      else
         QMessageBox::critical(this, tr("Error while cherry-pick"), errorMsg);
   }
}

void CommitHistoryContextMenu::applyPatch()
{
   const QString fileName(QFileDialog::getOpenFileName(this, "Select a patch to apply"));
   QScopedPointer<GitPatches> git(new GitPatches(mGit));

   if (!fileName.isEmpty() && git->applyPatch(fileName))
      emit signalRepositoryUpdated();
}

void CommitHistoryContextMenu::applyCommit()
{
   const QString fileName(QFileDialog::getOpenFileName(this, "Select a patch to apply"));
   QScopedPointer<GitPatches> git(new GitPatches(mGit));

   if (!fileName.isEmpty() && git->applyPatch(fileName, true))
      emit signalRepositoryUpdated();
}

void CommitHistoryContextMenu::push()
{
   QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
   QScopedPointer<GitRemote> git(new GitRemote(mGit));
   const auto ret = git->push();
   QApplication::restoreOverrideCursor();

   if (ret.output.toString().contains("has no upstream branch"))
   {
      const auto currentBranch = mGit->getCurrentBranch();
      BranchDlg dlg({ currentBranch, BranchDlgMode::PUSH_UPSTREAM, mGit });
      const auto ret = dlg.exec();

      if (ret == QDialog::Accepted)
         emit signalRepositoryUpdated();
   }
   else if (ret.success)
      emit signalRepositoryUpdated();
   else
      QMessageBox::critical(this, tr("Error while pushin"), ret.output.toString());
}

void CommitHistoryContextMenu::pull()
{
   QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
   QScopedPointer<GitRemote> git(new GitRemote(mGit));
   const auto ret = git->pull();
   QApplication::restoreOverrideCursor();

   if (ret.success)
      emit signalRepositoryUpdated();
   else
   {
      const auto errorMsg = ret.output.toString();

      if (errorMsg.contains("error: could not apply", Qt::CaseInsensitive)
          && errorMsg.contains("causing a conflict", Qt::CaseInsensitive))
      {
         emit signalPullConflict();
      }
      else
         QMessageBox::critical(this, tr("Error while pulling"), errorMsg);
   }
}

void CommitHistoryContextMenu::fetch()
{
   QScopedPointer<GitRemote> git(new GitRemote(mGit));

   if (git->fetch())
      emit signalRepositoryUpdated();
}

void CommitHistoryContextMenu::resetSoft()
{
   QScopedPointer<GitLocal> git(new GitLocal(mGit));

   if (git->resetCommit(mShas.first(), GitLocal::CommitResetType::SOFT))
      emit signalRepositoryUpdated();
}

void CommitHistoryContextMenu::resetMixed()
{
   QScopedPointer<GitLocal> git(new GitLocal(mGit));

   if (git->resetCommit(mShas.first(), GitLocal::CommitResetType::MIXED))
      emit signalRepositoryUpdated();
}

void CommitHistoryContextMenu::resetHard()
{
   const auto retMsg = QMessageBox::warning(
       this, "Reset hard requested!", "Are you sure you want to reset the branch to this commit in a <b>hard</b> way?",
       QMessageBox::Ok, QMessageBox::Cancel);

   if (retMsg == QMessageBox::Ok)
   {
      QScopedPointer<GitLocal> git(new GitLocal(mGit));

      if (git->resetCommit(mShas.first(), GitLocal::CommitResetType::HARD))
         emit signalRepositoryUpdated();
   }
}

void CommitHistoryContextMenu::merge(const QString &branchFrom)
{
   QScopedPointer<GitRemote> git(new GitRemote(mGit));
   const auto currentBranch = mGit->getCurrentBranch();

   emit signalMergeRequired(currentBranch, branchFrom);
}

void CommitHistoryContextMenu::addBranchActions(const QString &sha)
{
   auto isCommitInCurrentBranch = false;
   const auto currentBranch = mGit->getCurrentBranch();
   const auto commitInfo = mCache->getCommitInfo(sha);
   const auto remoteBranches = commitInfo.getReferences(References::Type::RemoteBranches);
   const auto localBranches = commitInfo.getReferences(References::Type::LocalBranch);

   QScopedPointer<GitBranches> git(new GitBranches(mGit));
   const auto tracking = git->getTrackingBranches();
   QMap<QString, bool> branchTracking;

   if (remoteBranches.isEmpty())
   {
      for (const auto &branch : localBranches)
         branchTracking.insert(branch, true);
   }
   else
   {
      for (const auto &remote : remoteBranches)
      {
         if (tracking.contains(remote))
         {
            for (const auto &local : tracking[remote])
            {
               if (!branchTracking.contains(local))
                  branchTracking.insert(local, true);
            }
         }
         else
            branchTracking.insert(remote, false);
      }
   }

   QList<QAction *> branchesToCheckout;

   for (const auto &pair : branchTracking.toStdMap())
   {
      isCommitInCurrentBranch |= pair.first == currentBranch;

      if (!branchTracking.isEmpty() && pair.first != currentBranch
          && pair.first != QString("origin/%1").arg(currentBranch))
      {
         const auto checkoutCommitAction = new QAction(QString(tr("%1")).arg(pair.first));
         checkoutCommitAction->setData(pair.second);
         connect(checkoutCommitAction, &QAction::triggered, this, &CommitHistoryContextMenu::checkoutBranch);
         branchesToCheckout.append(checkoutCommitAction);
      }
   }

   if (!branchesToCheckout.isEmpty())
   {
      const auto branchMenu = addMenu("Checkout branch...");
      branchMenu->addActions(branchesToCheckout);
   }

   if (!isCommitInCurrentBranch)
   {
      for (const auto &pair : branchTracking.toStdMap())
      {
         if (!pair.first.isEmpty() && pair.first != currentBranch
             && pair.first != QString("origin/%1").arg(currentBranch))
         {
            // If is the last commit of a branch
            const auto mergeBranchAction = addAction(QString(tr("Merge %1")).arg(pair.first));
            connect(mergeBranchAction, &QAction::triggered, this, [this, pair]() { merge(pair.first); });
         }
      }
   }

   addSeparator();

   if (!isCommitInCurrentBranch)
   {
      const auto cherryPickAction = addAction(tr("Cherry pick commit"));
      connect(cherryPickAction, &QAction::triggered, this, &CommitHistoryContextMenu::cherryPickCommit);
   }
}
