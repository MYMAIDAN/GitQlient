#include "PrChangesList.h"

#include <DiffHelper.h>
#include <GitHistory.h>
#include <FileDiffView.h>
#include <PrChangeListItem.h>
#include <PullRequest.h>

#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>

using namespace GitServer;

PrChangesList::PrChangesList(const QSharedPointer<GitBase> &git, QWidget *parent)
   : QFrame(parent)
   , mGit(git)
{
}

void PrChangesList::loadData(const QString &baseBranch, const QString &headBranch)
{
   Q_UNUSED(headBranch);
   Q_UNUSED(baseBranch);

   QScopedPointer<GitHistory> git(new GitHistory(mGit));
   const auto ret = git->getBranchesDiff(baseBranch, headBranch);

   if (ret.success)
   {
      auto diff = ret.output.toString();
      auto changes = DiffHelper::splitDiff(diff);

      if (!changes.isEmpty())
      {
         delete layout();

         const auto mainLayout = new QVBoxLayout();
         mainLayout->setContentsMargins(10, 10, 10, 10);
         mainLayout->setSpacing(0);

         for (auto &change : changes)
         {
            const auto changeListItem = new PrChangeListItem(change);
            mListItems.append(changeListItem);

            mainLayout->addWidget(changeListItem);
            mainLayout->addSpacing(10);
         }

         const auto mIssuesFrame = new QFrame();
         mIssuesFrame->setObjectName("IssuesViewFrame");
         mIssuesFrame->setLayout(mainLayout);

         const auto mScroll = new QScrollArea();
         mScroll->setWidgetResizable(true);
         mScroll->setWidget(mIssuesFrame);

         const auto aLayout = new QVBoxLayout(this);
         aLayout->setContentsMargins(QMargins());
         aLayout->setSpacing(0);
         aLayout->addWidget(mScroll);
      }
   }
}

void PrChangesList::onReviewsReceived(PullRequest pr)
{
   using Bookmark = QPair<int, int>;
   QMultiMap<QString, Bookmark> bookmarksPerFile;

   auto comments = pr.reviewComment;

   for (const auto &review : pr.reviews)
   {
      QMap<int, QVector<CodeReview>> reviews;
      QVector<int> codeReviewIds;
      QVector<QLayout *> listOfCodeReviews;

      auto iter = comments.begin();

      while (iter != comments.end())
      {
         if (iter->reviewId == review.id)
         {
            codeReviewIds.append(iter->id);
            reviews[iter->id].append(*iter);
            comments.erase(iter);
         }
         else if (codeReviewIds.contains(iter->replyToId))
         {
            reviews[iter->replyToId].append(*iter);
            comments.erase(iter);
         }
         else
            ++iter;
      }

      if (!reviews.isEmpty())
      {
         for (auto &codeReviews : reviews)
         {
            std::sort(codeReviews.begin(), codeReviews.end(),
                      [](const CodeReview &r1, const CodeReview &r2) { return r1.creation < r2.creation; });

            const auto first = codeReviews.constFirst();

            if (!first.outdated)
               bookmarksPerFile.insert(first.diff.file, { first.diff.line, first.id });
            else
               bookmarksPerFile.insert(first.diff.file, { first.diff.originalLine, first.id });
         }
      }
   }

   for (auto iter : mListItems)
   {
      QMap<int, int> bookmarks;

      for (auto bookmark : bookmarksPerFile.values(iter->getFileName()))
      {
         if (iter->getStartingLine() >= bookmark.first)
            bookmarks.insert(bookmark.first, bookmark.second);
      }

      if (!bookmarks.isEmpty())
         iter->setBookmarks(bookmarks);
   }
}
