#!/usr/bin/perl
use File::Basename;
my @files = ();
print "const char* internal_packed_files[] = {\n";
sub read_folder { 
  my ($path) = @_; 
  for my $file (map { basename($_) } glob("$path/*")) { 
    next if $file eq "." || $file eq "..";
    if (-d "$path/$file") { 
      read_folder("$path/$file"); 
    } else { 
      push(@files, "$path/$file");
    } 
  } 
}  
read_folder("data"); 
for my $path (sort(@files)) {
    print "\"$path\", \"";
    open(my $fh, "<", $path) or die $!;
    if ($path =~ m/\.lua/) {
      while(<$fh>) { 
        chomp; 
        print ($_ =~ s/\\/\\\\/gr =~ s/"/\\"/gr);
        print '\n'; 
      }
    } else {
      read $fh, my $file_content, -s $fh;
      print(join("", map { "\\x" . unpack("H*", $_) } split(//, $file_content)));
    }
    print "\",\n";
}
print("(void*)0, (void*)0 };")
