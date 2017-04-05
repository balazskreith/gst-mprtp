#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#include <pcap.h>

typedef struct{
  gdouble avg;
  gdouble std;
}Cell;

typedef struct{
  Cell gp,fec,lr,ffre,nlf,owd;
}Stat;

typedef struct{
  gdouble sum;
  gdouble sumsq;
}CalcHelper;

typedef struct{

  Stat fractal50;
  Stat fractal100;
  Stat fractal300;

  Stat scream50;
  Stat scream100;
  Stat scream300;

}Flow;

static void _calc_stat_helper(FILE* fp, Stat* stat){
  CalcHelper gph = {0.,0.};
  CalcHelper fech = {0.,0.};
  CalcHelper lrh = {0.,0.};
  CalcHelper ffreh = {0.,0.};
  CalcHelper nlfh = {0.,0.};
  CalcHelper owdh = {0.,0.};
  gdouble gp,fec,lr,ffre,owd;
  gint nlf;
  guint count = 0;
  gchar line[256];

  //read while file
  while (fgets(line, 1024, fp)){
    sscanf(line, "%f,%f,%f,%f,%d,%f", &gp, &fec, &lr, &ffre, &nlf, &owd);
    ++count;
  }

  gph.sum += gp;
  gph.sumsq += gp*gp;
  fech.sum += fec;
  fech.sumsq += fec*fec;
  lrh.sum += lr;
  lrh.sumsq += lr*lr;
  ffreh.sum += ffre;
  ffreh.sumsq += ffre*ffre;
  nlfh.sum += nlf;
  nlfh.sumsq += nlf*nlf;
  owdh.sum += owd;
  owdh.sumsq += owd*owd;

  stat->gp.avg = gph.sum / (gdouble) count;
  stat->gp.std = sqrt(gph.sumsq / (gdouble) count - (stat->gp.avg * stat->gp.avg));
  stat->fec.avg = fech.sum / (gdouble) count;
  stat->fec.std = sqrt(fech.sumsq / (gdouble) count - (stat->fec.avg * stat->fec.avg));
  stat->lr.avg = lrh.sum / (gdouble) count;
  stat->lr.std = sqrt(lrh.sumsq / (gdouble) count - (stat->lr.avg * stat->lr.avg));
  stat->ffre.avg = ffreh.sum / (gdouble) count;
  stat->ffre.std = sqrt(ffreh.sumsq / (gdouble) count - (stat->ffre.avg * stat->ffre.avg));
  stat->nlf.avg = nlfh.sum / (gdouble) count;
  stat->nlf.std = sqrt(nlfh.sumsq / (gdouble) count - (stat->nlf.avg * stat->nlf.avg));
  stat->owd.avg = owdh.sum / (gdouble) count;
  stat->owd.std = sqrt(owdh.sumsq / (gdouble) count - (stat->owd.avg * stat->owd.avg));
}

static void _calc_stat(Stat* stat, const gchar* path, const gchar *filename){
  gchar file[1024];
  FILE* fp;
  memset(file, 0, 1024);
  sprintf(file, "%s/%s", path, file);
  if(!(fp = fopen(file, "r"))){
    g_print("%s not exits\n", file);
    return;
  }
  _calc_stat_helper(fp, stat);
  fclose(fp);
}

static void _calc_flow(Flow* flow, const gchar *path, const gchar* prefix){
  gchar filename[1024];
  sprintf(filename, "%s%s", prefix,"fractal_50ms.csv");
  _calc_stat(&flow->fractal50, path, filename);

  sprintf(filename, "%s%s", prefix,"fractal_100ms.csv");
  _calc_stat(&flow->fractal100, path, filename);

  sprintf(filename, "%s%s", prefix,"fractal_300ms.csv");
  _calc_stat(&flow->fractal300, path, filename);

  sprintf(filename, "%s%s", prefix,"scream_50ms.csv");
  _calc_stat(&flow->scream50, path, filename);

  sprintf(filename, "%s%s", prefix,"scream_100ms.csv");
  _calc_stat(&flow->scream100, path, filename);

  sprintf(filename, "%s%s", prefix,"scream_300ms.csv");
  _calc_stat(&flow->scream300, path, filename);
}

static Flow _fprintf_single_flow(FILE *fp, Flow* flow)
{
  fprintf(fp, "\begin{table}");
  fprintf(fp, "\centering" );
  fprintf(fp, "\begin{tabular}{llll}" );
  fprintf(fp, "\textbf{} & \textbf{} & \textbf{\FRACTaL} & \textbf{SCReAM} \\" );
  fprintf(fp, "\hline" );
  fprintf(fp, "\multirow{3}{*}{\rotatebox{90}{\textbf{50ms}}} & \textbf{GP {[}kbps{]}} & %4.2f $\pm$ %4.2f & %4.2f $\pm$ %4.2f \\",
      flow->fractal50.gp.avg, flow->fractal50.gp.std, flow->scream50.gp.avg, flow->scream50.gp.std);
  fprintf(fp, " & \textbf{LR {[}\%{]}} & %4.2f $\pm$ %4.2f & %4.2f $\pm$ %4.2f \\",
      flow->fractal50.lr.avg, flow->fractal50.lr.std, flow->scream50.lr.avg, flow->scream50.lr.std);
  fprintf(fp, "% & \textbf{FEC Rate {[}kbs{]}} & %4.2f $\pm$ %4.2f  & - \\",
      flow->fractal50.fec.avg, flow->fractal50.fec.std);
  fprintf(fp, "% & \textbf{FFRE} & %4.2f $\pm$ %4.2f & - \\" ,
      flow->fractal50.ffre.avg, flow->fractal50.ffre.std);
  fprintf(fp, " & \textbf{OWD {[}ms{]}} & %4.2f $\pm$ %4.2f & %4.2f $\pm$ %4.2f \\",
      flow->fractal50.owd.avg, flow->fractal50.owd.std, flow->scream50.owd.avg, flow->scream50.owd.std);
  fprintf(fp, " \hline" );
  fprintf(fp, "\multirow{3}{*}{\rotatebox{90}{\textbf{100ms}}} & \textbf{GP {[}kbps{]}} & %4.2f $\pm$ %4.2f & %4.2f $\pm$ %4.2f \\",
      flow->fractal100.gp.avg, flow->fractal100.gp.std, flow->scream100.gp.avg, flow->scream100.gp.std);
  fprintf(fp, " & \textbf{LR {[}\%{]}} & %4.2f $\pm$ %4.2f & %4.2f $\pm$ %4.2f \\",
      flow->fractal100.lr.avg, flow->fractal100.lr.std, flow->scream100.lr.avg, flow->scream100.lr.std);
  fprintf(fp, "% & \textbf{FEC Rate {[}kbs{]}} & %4.2f $\pm$ %4.2f  & - \\",
      flow->fractal100.fec.avg, flow->fractal100.fec.std);
  fprintf(fp, "% & \textbf{FFRE} & %4.2f $\pm$ %4.2f & - \\" ,
      flow->fractal100.ffre.avg, flow->fractal100.ffre.std);
  fprintf(fp, " & \textbf{OWD {[}ms{]}} & %4.2f $\pm$ %4.2f & %4.2f $\pm$ %4.2f \\",
      flow->fractal100.owd.avg, flow->fractal100.owd.std, flow->scream100.owd.avg, flow->scream100.owd.std);
  fprintf(fp, " \hline" );
  fprintf(fp, "\multirow{3}{*}{\rotatebox{90}{\textbf{50ms}}} & \textbf{GP {[}kbps{]}} & %4.2f $\pm$ %4.2f & %4.2f $\pm$ %4.2f \\",
      flow->fractal300.gp.avg, flow->fractal300.gp.std, flow->scream300.gp.avg, flow->scream300.gp.std);
  fprintf(fp, " & \textbf{LR {[}\%{]}} & %4.2f $\pm$ %4.2f & %4.2f $\pm$ %4.2f \\",
      flow->fractal300.lr.avg, flow->fractal300.lr.std, flow->scream300.lr.avg, flow->scream300.lr.std);
  fprintf(fp, "% & \textbf{FEC Rate {[}kbs{]}} & %4.2f $\pm$ %4.2f  & - \\",
      flow->fractal300.fec.avg, flow->fractal300.fec.std);
  fprintf(fp, "% & \textbf{FFRE} & %4.2f $\pm$ %4.2f & - \\" ,
      flow->fractal300.ffre.avg, flow->fractal300.ffre.std);
  fprintf(fp, " & \textbf{OWD {[}ms{]}} & %4.2f $\pm$ %4.2f & %4.2f $\pm$ %4.2f \\",
      flow->fractal300.owd.avg, flow->fractal300.owd.std, flow->scream300.owd.avg, flow->scream300.owd.std);
  fprintf(fp, "\end{tabular}" );
  fprintf(fp, "\caption{" );
  fprintf(fp, "TODO: CAPTION" );
  fprintf(fp, "}" );
  fprintf(fp, "\label{table:rmcat1}" );
  fprintf(fp, "\end{table}" );

}

int main (int argc, char **argv)
{
  FILE *fp;
  Flow* flows;
  gint flownum;
  if(argc < 3){
  usage:
    g_print("Usage: ./program flownum datstats_dir result_path\n");
    return 0;
  }
  flows = g_malloc0(sizeof(Flow) * 3);
  fp = fopen (argv[3],"w");
  flownum = atoi(argv[1]);
  if(flownum == 1){
    _calc_flow(flows, argv[2], "datstat_");
  }
  fclose(fp);
  g_print("Results are made in %s\n", argv[3]);
  return 0;
}
