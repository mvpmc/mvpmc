#! /usr/bin/perl

#
# Create by Dwayne Forsyth
# Dwayne@DForsyth.net
# Free for anyone to use.
#
# version 1.0   01/23/2004
#


sub get_name {
   my ($file,$offset) = @_;

   my $loop=1;
   my $c1,$c2;
   my $name = '';

   $file->seek($offset+8,0);
   while ($loop) {
      read($file, $buf, 2);
      ($c1,$c2) = unpack( "CC", $buf);
      $c1 = chr($c1);
      $c2 = chr($c2);
      if ($c1 =~ /[ A-Za-z]/) {$name.=$c1;
      } else {$loop=0};
      if ($c2 =~ /[ A-Za-z]/) {$name.=$c2;
      } else {$loop=0};
   }
   return($name); 
}

sub store_file {
   my ($file,$name,$start_offset,$end_offset) = @_;
   $file_out = new FileHandle ">".$name;

   $file->seek($start_offset,0);
   $file->read($buf,$end_offset-$start_offset);
   $file_out->write($buf);
   $file_out->close();
}




use FileHandle;

if ($#ARGV<0) {
   print "Error: Missing file name\nUsage split.pl <filename>\n\n";
   exit(1);
}
my $file =  new FileHandle "<".$ARGV[0];
#my $file =  new FileHandle "</tftpboot/zImage.embedded";
my $offset=0;
my $save_name = "boot_loader";
my $save_offset = 0;
my $new_name;

while (read($file, $buf, 2)) {
   $offset+=2;
   ($head) = unpack( "v", $buf); 
   if ($head == 0x8b1f) {
      $new_name = &get_name($file,$offset);
      if ($new_name =~ /vmlinux|ramdisk/) {
         printf "Found %s %8.8x\n",$new_name,$offset-2;
         printf "Store %s %8.8x %8.8x\n\n", $save_name,$save_offset,$offset-2;
         &store_file($file,$save_name,$save_offset,$offset-2);
         $save_name = $new_name;
         $save_offset=$offset-2;
      }
      $file->seek($offset,0);
   }
}
printf "Found %s %8.8x\n","_end_",$offset;
printf "Store %s %8.8x %8.8x\n", $save_name,$save_offset,$offset;
&store_file($file, $save_name,$save_offset,$offset);
