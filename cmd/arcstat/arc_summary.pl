#! /usr/bin/perl -w
# ported from http://cuddletech.com/arc_summary.html

use strict;

my $spl_size;
my $spl_alloc;
my %arcstats;
open(FD,"/proc/spl/kmem/slab") or die "opening /proc/spl/kmem/slab";
<FD>;<FD>;
while (<FD>)
{
	chomp($_);
	my @tmp = split (" ",$_);
	$spl_size+=$tmp[2];
	$spl_alloc+=$tmp[3];
}
close(FD);

open(FD,"/proc/spl/kstat/zfs/arcstats");
<FD>;<FD>;
while (<FD>)
{
	chomp($_);
	my @tmp=split(" ",$_);
	$arcstats{$tmp[0]}=$tmp[2];
}
close(FD);
my $arc_size=$arcstats{"size"};
my $mru_size=$arcstats{"p"};
my $target_size=$arcstats{"c"};
my $arc_min_size=$arcstats{"c_min"};
my $arc_max_size=$arcstats{"c_max"};
my $arc_metadata_limit=$arcstats{"arc_meta_limit"};
my $arc_metadata_max=$arcstats{"arc_meta_max"};
my $arc_metadata_used=$arcstats{"arc_meta_used"};


print "ARC Size:\n";
printf("\t Current Size:             %d MB (arcsize)\n", $arc_size / 1024 / 1024);
printf("\t Target Size (Adaptive):   %d MB (c)\n", $target_size / 1024 / 1024);
printf("\t Min Size (Hard Limit):    %d MB (zfs_arc_min)\n", $arc_min_size / 1024 / 1024);
printf("\t Max Size (Hard Limit):    %d MB (zfs_arc_max)\n", $arc_max_size / 1024 / 1024);
printf("\t Metadata Max Size :       %d MB\n", $arc_metadata_max / 1024 / 1024);
printf("\t Metadata Limit Size :     %d MB\n", $arc_metadata_limit / 1024 / 1024);
printf("\t Metadata Used Size :      %d MB\n", $arc_metadata_used / 1024 / 1024);

print ("SPL Memory Usage:\n");
printf ("\tSPL slab allocated:       %d MB\n",$spl_size / 1024 / 1024);
printf ("\tSPL slab used:            %d MB\n",$spl_alloc / 1024 / 1024);

my $mfu_size = $target_size - $mru_size;
my $mru_perc = 100*($mru_size / $target_size);
my $mfu_perc = 100*($mfu_size / $target_size);

print "\nARC Size Breakdown:\n";

printf("\t Most Recently Used Cache Size: \t %2d%% \t%d MB (p)\n", $mru_perc, $mru_size / 1024 / 1024);
printf("\t Most Frequently Used Cache Size: \t %2d%% \t%d MB (c-p)\n", $mfu_perc, $mfu_size / 1024 / 1024);
print "\n";

my $arc_hits = $arcstats{"hits"};
my $arc_misses = $arcstats{"misses"};
my $arc_accesses_total = $arc_hits + $arc_misses;
my $arc_hit_perc = 100*($arc_hits / $arc_accesses_total);
my $arc_miss_perc = 100*($arc_misses / $arc_accesses_total);

my $mfu_hits =  $arcstats{"mfu_hits"};
my $mru_hits =  $arcstats{"mru_hits"};
my $mfu_ghost_hits = $arcstats{"mfu_ghost_hits"};
my $mru_ghost_hits = $arcstats{"mru_ghost_hits"};
my $anon_hits = $arc_hits - ($mfu_hits + $mru_hits + $mfu_ghost_hits + $mru_ghost_hits);

my $real_hits = $mfu_hits + $mru_hits;
my $real_hits_perc = 100*($real_hits / $arc_accesses_total);

my $anon_hits_perc = 100*($anon_hits / $arc_hits);
my $mfu_hits_perc = 100*($mfu_hits / $arc_hits);
my $mru_hits_perc = 100*($mru_hits / $arc_hits);
my $mfu_ghost_hits_perc = 100*($mfu_ghost_hits / $arc_hits);
my $mru_ghost_hits_perc = 100*($mru_ghost_hits / $arc_hits);

my $demand_data_hits = $arcstats{"demand_data_hits"};
my $demand_metadata_hits = $arcstats{"demand_metadata_hits"};
my $prefetch_data_hits = $arcstats{"prefetch_data_hits"};
my $prefetch_metadata_hits = $arcstats{"prefetch_metadata_hits"};
my $demand_data_hits_perc = 100*($demand_data_hits / $arc_hits);
my $demand_metadata_hits_perc = 100*($demand_metadata_hits / $arc_hits);
my $prefetch_data_hits_perc = 100*($prefetch_data_hits / $arc_hits);
my $prefetch_metadata_hits_perc = 100*($prefetch_metadata_hits / $arc_hits);
my $demand_data_misses = $arcstats{"demand_data_misses"};
my $demand_metadata_misses = $arcstats{"demand_metadata_misses"};
my $prefetch_data_misses = $arcstats{"prefetch_data_misses"};
my $prefetch_metadata_misses = $arcstats{"prefetch_metadata_misses"};
my $demand_data_misses_perc = 100*($demand_data_misses / $arc_misses);
my $demand_metadata_misses_perc = 100*($demand_metadata_misses / $arc_misses);
my $prefetch_data_misses_perc = 100*($prefetch_data_misses / $arc_misses);
my $prefetch_metadata_misses_perc = 100*($prefetch_metadata_misses / $arc_misses);
my $prefetch_data_total = $prefetch_data_hits + $prefetch_data_misses;
my $prefetch_data_perc = 0;
if ( $prefetch_data_total > 0 )
{
	$prefetch_data_perc = 100*($prefetch_data_hits / $prefetch_data_total);
}

my $demand_data_total = $demand_data_hits + $demand_data_misses;
my $demand_data_perc=0;
if ( $demand_data_total )
{
	$demand_data_perc = 100*($demand_data_hits / $demand_data_total);
}


print "ARC Efficency:\n";
printf("\t Cache Access Total:        \t %d\n", $arc_accesses_total);
printf("\t Cache Hit Ratio:      %2d%%\t %d   \t[Defined State for buffer]\n", $arc_hit_perc, $arc_hits);
printf("\t Cache Miss Ratio:     %2d%%\t %d   \t[Undefined State for Buffer]\n", $arc_miss_perc, $arc_misses);
printf("\t REAL Hit Ratio:       %2d%%\t %d   \t[MRU/MFU Hits Only]\n", $real_hits_perc, $real_hits);
print "\n";
printf("\t Data Demand   Efficiency:    %2d%%\n", $demand_data_perc);
if ($prefetch_data_total == 0){ 
        printf("\t Data Prefetch Efficiency:    DISABLED (zfs_prefetch_disable)\n");
} else {
        printf("\t Data Prefetch Efficiency:    %2d%%\n", $prefetch_data_perc);
}
print "\n";


print "\tCACHE HITS BY CACHE LIST:\n";
if ( $anon_hits < 1 ){
        printf("\t  Anon:                       --%% \t Counter Rolled.\n");
} else {
        printf("\t  Anon:                       %2d%% \t %d            \t[ New Customer, First Cache Hit ]\n", $anon_hits_perc, $anon_hits);
}
printf("\t  Most Recently Used:         %2d%% \t %d (mru)      \t[ Return Customer ]\n", $mru_hits_perc, $mru_hits);
printf("\t  Most Frequently Used:       %2d%% \t %d (mfu)      \t[ Frequent Customer ]\n", $mfu_hits_perc, $mfu_hits);
printf("\t  Most Recently Used Ghost:   %2d%% \t %d (mru_ghost)\t[ Return Customer Evicted, Now Back ]\n", $mru_ghost_hits_perc, $mru_ghost_hits);
printf("\t  Most Frequently Used Ghost: %2d%% \t %d (mfu_ghost)\t[ Frequent Customer Evicted, Now Back ]\n", $mfu_ghost_hits_perc, $mfu_ghost_hits);

print "\tCACHE HITS BY DATA TYPE:\n";
printf("\t  Demand Data:                %2d%% \t %d \n", $demand_data_hits_perc, $demand_data_hits);
printf("\t  Prefetch Data:              %2d%% \t %d \n", $prefetch_data_hits_perc, $prefetch_data_hits);
printf("\t  Demand Metadata:            %2d%% \t %d \n", $demand_metadata_hits_perc, $demand_metadata_hits);
printf("\t  Prefetch Metadata:          %2d%% \t %d \n", $prefetch_metadata_hits_perc, $prefetch_metadata_hits);

print "\tCACHE MISSES BY DATA TYPE:\n";
printf("\t  Demand Data:                %2d%% \t %d \n", $demand_data_misses_perc, $demand_data_misses);
printf("\t  Prefetch Data:              %2d%% \t %d \n", $prefetch_data_misses_perc, $prefetch_data_misses);
printf("\t  Demand Metadata:            %2d%% \t %d \n", $demand_metadata_misses_perc, $demand_metadata_misses);
printf("\t  Prefetch Metadata:          %2d%% \t %d \n", $prefetch_metadata_misses_perc, $prefetch_metadata_misses);

print "---------------------------------------------\n"
