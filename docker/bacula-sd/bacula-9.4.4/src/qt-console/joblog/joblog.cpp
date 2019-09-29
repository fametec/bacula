/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

   The original author of Bacula is Kern Sibbald, with contributions
   from many others, a complete list can be found in the file AUTHORS.

   You may use this file and others of this release according to the
   license defined in the LICENSE file, which includes the Affero General
   Public License, v3.0 ("AGPLv3") and some additional permissions and
   terms pursuant to its AGPLv3 Section 7.

   This notice must be preserved when any source code is 
   conveyed and/or propagated.

   Bacula(R) is a registered trademark of Kern Sibbald.
*/
 
/*
 *  JobLog Class
 *
 *   Dirk Bartley, March 2007
 *
 */ 

#include "bat.h"
#include "joblog.h"

JobLog::JobLog(QString &jobId, QTreeWidgetItem *parentTreeWidgetItem) : Pages()
{
   setupUi(this);
   pgInitialize(tr("JobLog"), parentTreeWidgetItem);
   QTreeWidgetItem* thisitem = mainWin->getFromHash(this);
   thisitem->setIcon(0,QIcon(QString::fromUtf8(":images/joblog.png")));
   m_cursor = new QTextCursor(textEdit->document());

   m_jobId = jobId;
   getFont();
   populateText();

   dockPage();
   setCurrent();
}

void JobLog::getFont()
{
   QFont font = textEdit->font();

   QString dirname;
   m_console->getDirResName(dirname);
   QSettings settings(dirname, "bat");
   settings.beginGroup("Console");
   font.setFamily(settings.value("consoleFont", "Courier").value<QString>());
   font.setPointSize(settings.value("consolePointSize", 10).toInt());
   font.setFixedPitch(settings.value("consoleFixedPitch", true).toBool());
   settings.endGroup();
   textEdit->setFont(font);
}

/*
 * Populate the text in the window
 */
void JobLog::populateText()
{
   QString query;
   query = "SELECT Time, LogText FROM Log WHERE JobId='" + m_jobId + "' order by Time";

   /* This could be a log item */
   if (mainWin->m_sqlDebug) {
      Pmsg1(000, "Log query cmd : %s\n", query.toUtf8().data());
   }
  
   QStringList results;
   if (m_console->sql_cmd(query, results)) {

      if (!results.size()) {
         QMessageBox::warning(this, tr("Bat"),
            tr("There were no results!\n"
               "It is possible you may need to add \"catalog = all\" "
               "to the Messages resource for this job.\n"), QMessageBox::Ok);
         return;
      } 

      QString jobstr("JobId "); /* FIXME: should this be translated ? */
      jobstr += m_jobId;

      QString htmlbuf("<html><body><b>" + tr("Log records for job %1").arg(m_jobId) );
      htmlbuf += "</b><table>";
  
      /* Iterate through the lines of results. */
      QString field;
      QStringList fieldlist;
      QString lastTime;
      QString lastSvc;
      foreach (QString resultline, results) {
         fieldlist = resultline.split("\t");
         
         if (fieldlist.size() < 2)
            continue;

         htmlbuf +="<tr>";

         QString curTime = fieldlist[0].trimmed();

         field = fieldlist[1].trimmed();
         int colon = field.indexOf(":");
         if (colon > 0) {
            /* string is like <service> <jobId xxxx>: ..." 
             * we split at ':' then remove the jobId xxxx string (always the same) */ 
            QString curSvc(field.left(colon).replace(jobstr,"").trimmed());
            if (curSvc == lastSvc  && curTime == lastTime) {
               curTime.clear();
               curSvc.clear(); 
            } else {
               lastTime = curTime;
               lastSvc = curSvc;
            }
            htmlbuf += "<td>" + curTime + "</td>";
            htmlbuf += "<td><p>" + curSvc + "</p></td>";

            /* rest of string is marked as pre-formatted (here trimming should
             * be avoided, to preserve original formatting) */
            QString msg(field.mid(colon+2));
            if (msg.startsWith( tr("Error:")) ) { /* FIXME: should really be translated ? */
               /* error msg, use a specific class */
               htmlbuf += "<td><pre class=err>" + msg + "</pre></td>";
            } else {
               htmlbuf += "<td><pre>" + msg + "</pre></td>";
            }
         } else {
            /* non standard string, place as-is */
            if (curTime == lastTime) {
               curTime.clear();
            } else {
               lastTime = curTime;
            }
            htmlbuf += "<td>" + curTime + "</td>";
            htmlbuf += "<td><pre>" + field + "</pre></td>";
         }

         htmlbuf += "</tr>";
  
      } /* foreach resultline */

      htmlbuf += "</table></body></html>";

      /* full text ready. Here a custom sheet is used to align columns */
      QString logSheet("p,pre,.err {margin-left: 10px} .err {color:#FF0000;}");
      textEdit->document()->setDefaultStyleSheet(logSheet);
      textEdit->document()->setHtml(htmlbuf); 
      textEdit->moveCursor(QTextCursor::Start);

   } /* if results from query */
  
}
  
