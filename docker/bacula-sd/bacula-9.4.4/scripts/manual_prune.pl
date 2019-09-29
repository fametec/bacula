#!/usr/bin/perl -w
#
# Copyright (C) 2000-2015 Kern Sibbald
# License: BSD 2-Clause; see file LICENSE-FOSS
#
use strict;

=head1 DESCRIPTION

    manual_prune.pl -- prune volumes

=head2 USAGE

    manual_prune.pl [--bconsole=/path/to/bconsole] [--help] [--doprune] [--expired] [--fixerror] [--fileprune]

    This program when run will manually prune all Volumes that it finds
    in your Bacula catalog. It will respect all the Retention periods.
    
    manual_prune must have access to bconsole. It will execute bconsole
      from /opt/bacula/bin.  If bconsole is in a different location,
      you must specify the path to it with the --bconsole=... option.

    If you do not add --doprune, you will see what the script proposes
      to do, but it will not prune.

    If you add --fixerror, it will change the status of any Volume
      that is marked Error to Used so that it will be pruned.

    If you add --expired, it will attempt to prune only those 
      Volumes where the Volume Retention period has expired.

    If you use --fileprune, the script will prune files and pathvisibility
      useful to avoid blocking Bacula during pruning.

    Adding --debug will print additional debug information.


=head1 LICENSE

   Copyright (C) 2008-2014 Bacula Systems SA

   Bacula(R) is a registered trademark of Kern Sibbald.
   The licensor of Bacula Enterprise is Bacula Systems SA,
   Rue Galilee 5, 1400 Yverdon-les-Bains, Switzerland.

  This file has been made available for your personal use in the
  hopes that it will allow community users to make better use of
  Bacula.

=head1 VERSION

    1.4

=cut

use Getopt::Long qw/:config no_ignore_case/;
use Pod::Usage;
use File::Temp;

my $help;
my $do_prune;
my $expired;
my $debug;
# set to your bconsole prog
my $bconsole = "/opt/bacula/bin/bconsole";
my $do_fix;
my $do_file_prune;

GetOptions('help'       => \$help,
           'bconsole=s' => \$bconsole,
           'expired'    => \$expired,
           'debug'      => \$debug,
           'fixerror'   => \$do_fix,
           'fileprune'  => \$do_file_prune,
           'doprune'    => \$do_prune)
    || Pod::Usage::pod2usage(-exitval => 2, -verbose => 2) ;

if ($help) {
    Pod::Usage::pod2usage(-exitval => 2, -verbose => 2) ;
}

if (! -x $bconsole) {
    die "Can't exec $bconsole, please specify --bconsole option $!";
}

my @vol;
my @vol_purged;

# This fix can work with File based device. Don't use it for Tape media
if ($do_fix) {
    my ($fh, $file) = File::Temp::tempfile();
    print $fh "sql
SELECT VolumeName AS \"?vol?\" FROM Media WHERE VolStatus = 'Error';

";
    close($fh);
    open(FP, "cat $file | $bconsole|") or die "Can't open $bconsole (ERR=$!), adjust your \$PATH";
    while (my $l = <FP>)
    {
        if ($l =~ /^\s*\|\s*([\w\d:\. \-]+?)\s*\|/) {
            if ($debug) {
                print $l;
            }
            push @vol, $1;
        } 
    }
    close(FP);
    unlink($file);

    if (scalar(@vol) > 0) {
        print "Will try to fix volume in Error: ", join(",", @vol), "\n";
        open(FP, "|$bconsole") or die "Can't send commands to $bconsole";
        print FP map { "update volume=$_ volstatus=Used\n" } @vol; 
        close(FP);
        @vol = ();
    }
}

if ($do_file_prune) {
    my ($fh, $file) = File::Temp::tempfile();
    if ($do_prune) {
        print $fh "sql
BEGIN;
CREATE TEMPORARY TABLE temp AS
SELECT DISTINCT JobId FROM Job JOIN JobMedia USING (JobId) JOIN
(SELECT Media.MediaId  AS MediaId 
FROM      Media 
WHERE   VolStatus IN ('Full', 'Used')
  AND (    (Media.LastWritten) 
         +  interval '1 second' * (Media.VolRetention)
      ) < NOW()) AS M USING (MediaId)
    WHERE Job.JobFiles > 50000 AND Job.PurgedFiles=0;
SELECT JobId FROM temp;
DELETE FROM File WHERE JobId IN (SELECT JobId FROM temp);
DELETE FROM PathVisibility WHERE JobId IN (SELECT JobId FROM temp);
UPDATE Job SET PurgedFiles=1 WHERE JobId IN (SELECT JobId FROM temp);
DROP TABLE temp;
COMMIT;

quit
";
    } else {
        print $fh "sql
SELECT DISTINCT JobId FROM Job JOIN JobMedia USING (JobId) JOIN
(SELECT Media.MediaId  AS MediaId 
FROM      Media 
WHERE   VolStatus IN ('Full', 'Used')
  AND (    (Media.LastWritten) 
         +  interval '1 second' * (Media.VolRetention)
      ) < NOW()) AS M USING (MediaId)
    WHERE Job.JobFiles > 50000 AND Job.PurgedFiles=0;

quit
";
    }
    close($fh);
    open(FP, "cat $file | $bconsole|") or die "Can't open $bconsole (ERR=$!), adjust your \$PATH";
    while (my $l = <FP>)
    {
       if ($debug || !$do_prune) {
          print $l;
       }
    }
    close(FP);
    unlink($file);
    exit 0;
}

# TODO: Fix it for SQLite
# works only for postgresql and MySQL at the moment
# One of the two query will fail, but it's not a problem
if ($expired) {
    my ($fh, $file) = File::Temp::tempfile();
    print $fh "sql
SELECT Media.VolumeName  AS volumename, 
       Media.LastWritten AS lastwritten,
       (
          (Media.LastWritten) 
        +  interval '1 second' * (Media.VolRetention)
       ) AS expire
FROM      Media 
WHERE   VolStatus IN ('Full', 'Used')
  AND (    (Media.LastWritten) 
         +  interval '1 second' * (Media.VolRetention)
      ) < NOW();
SELECT Media.VolumeName  AS volumename, 
       Media.LastWritten AS lastwritten,
       (
          Media.LastWritten +  Media.VolRetention
       ) AS expire
FROM      Media 
WHERE   VolStatus IN ('Full', 'Used')
  AND (    Media.LastWritten +  Media.VolRetention
      ) < NOW();

quit
";
    close($fh);
    open(FP, "cat $file | $bconsole|") or die "Can't open $bconsole (ERR=$!), adjust your \$PATH";
    while (my $l = <FP>)
    {
        #  | TestVolume001 | 2011-06-17 14:36:59 | 2011-06-17 14:37:00
        if ($l =~ /^\s*\|\s*([\w\d:\. \-]+?)\s*\|\s*\d/) {
            if ($debug) {
                print $l;
            }
            push @vol, $1;
        }
    }
    close(FP);
    unlink($file);

} else {

    open(FP, "echo list volumes | $bconsole|") or die "Can't open $bconsole (ERR=$!), adjust your \$PATH";
    while (my $l = <FP>)
    {
                  # |        1 |   TestVolume001 | Used
        if ($l =~ /^\s*\|\s*[\d,]+\s*\|\s*([\w\d-]+)\s*\|\s*Used/) {
            push @vol, $1;
        }
        if ($l =~ /^\s*\|\s*[\d,]+\s*\|\s*([\w\d-]+)\s*\|\s*Full/) {
            push @vol, $1;
        }
        if ($l =~ /^\s*\|\s*[\d,]+\s*\|\s*([\w\d-]+)\s*\|\s*Purged/) {
            push @vol_purged, $1;
        }
    }
    close(FP);

    if ($? != 0) {
        system("echo list volumes | $bconsole");
        die "bconsole returns a non zero status, please check that you can execute it";
        
    }
}

if (!scalar(@vol)) {
    print "No Volume(s) found to prune.\n";

} else {
    if ($do_prune) {
        print "Attempting to to prune ", join(",", @vol), "\n";
        open(FP, "|$bconsole") or die "Can't send commands to $bconsole";
        print FP map { "prune volume=$_ yes\n" } @vol; 
        close(FP);
    } else {
        print "Would have attempted to prune ", join(",", @vol), "\n";
        print "You can actually prune by specifying the --doprune option.\n"
    }
}
