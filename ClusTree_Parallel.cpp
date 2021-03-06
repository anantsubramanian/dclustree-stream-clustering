#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <queue>
#include <cstring>
#include <sys/time.h>
#include <unistd.h>
#include <cstdlib>
#include <mpi.h>

using namespace std;

#define LAMBDA 0.00001
#define BETA 2
#define NUMINSERTS 300
#define MASTER 0
#define INITIALSIZE 30
#define UPDATECHECK 50
#define SENTINEL -99999

typedef pair<int, int> II;
int points = 0;

class Point
{
	public:
		int timestamp;
		int x, y;
		Point(int x, int y, int timestamp)
		{
			this->x = x;
			this->y = y;
			this->timestamp = timestamp;
		}

};

class CF
{
	public:
		double n;
		double lsx, ssx, lsy, ssy;
		int timestamp;
		
		CF(int lsx, int lsy, int timestamp, double n)
		{
			this->n = n;
			this->lsx = lsx;
			this->lsy = lsy;
			this->ssx = this->lsx * this->lsx;
			this->ssy = this->lsy * this->lsy;
			this->timestamp = timestamp;
		}

		CF(double n, double lsx, double lsy, double ssx, double ssy, int timestamp)
		{
			this->n = n;
			this->lsx = lsx;
			this->lsy = lsy;
			this->ssx = ssx;
			this->ssy = ssy;
			this->timestamp = timestamp;
		}
		
		CF()
		{
			// Default constructor not used
		}

		void update(int timestamp)
		{
			this->n *= pow(BETA,-LAMBDA*(timestamp - this->timestamp));
			this->lsx *= pow(BETA,-LAMBDA*(timestamp - this->timestamp));
			this->lsy *= pow(BETA,-LAMBDA*(timestamp - this->timestamp));
			this->ssx *= pow(BETA,-LAMBDA*(timestamp - this->timestamp));
			this->ssy *= pow(BETA,-LAMBDA*(timestamp - this->timestamp));
			this->timestamp = timestamp;
		}

};

class Node
{
	public:
		bool isleaf;
		int size;
		CF **cf;
		Node **child;
		Node *parent;
		
		Node(Node *parent, int m, int M, int l, int L, bool isleaf)
		{
			this->size = 0;
			this->cf = new CF*[M+1];
			this->child = new Node*[M+1];
			for(int i = 0; i < M+1; i++)
			{
				this->cf[i] = NULL;
				this->child[i] = NULL;
			}
			this->parent = parent;
			this->isleaf = isleaf;
		}

		void addCF(CF *cf, Node *relatedchild)
		{
			// Invoked during splits
			this->cf[this->size] = cf;
			this->child[this->size] = relatedchild;
			this->size++;
		}

		void addCF(double n, double lsx, double lsy, double ssx, double ssy, int timestamp, Node *relatedchild)
		{
			this->cf[this->size] = new CF(n, lsx, lsy, ssx, ssy, timestamp);
			this->child[this->size] = relatedchild;
			this->size++;
		}

		void addPoint(Point *p)
		{
			// Only called on a leaf node instance
			// child pointers remain NULL
			this->cf[this->size] = new CF(p->x, p->y, p->timestamp, 1);
			this->size++;
		}

		CF* getCF(int newtimestamp)
		{
			double n = 0.0, lsx = 0.0, lsy = 0.0, ssx = 0.0, ssy = 0.0;
			for(int i = 0; i < this->size; i++)
			{
				n += this->cf[i]->n;
				lsx += this->cf[i]->lsx;
				lsy += this->cf[i]->lsy;
				ssx += this->cf[i]->ssx;
				ssy += this->cf[i]->ssy;
			}

			return new CF(n, lsx, lsy, ssx, ssy, newtimestamp);
		}

};

typedef pair<Node*, int> NI;

class ClusTree
{
	int m, M, l, L;
	Node *root;
	int deltaT, lastpointtime;
	public:
		ClusTree(int m, int M, int l, int L)
		{
			this->m = m;
			this->M = M;
			this->l = l;
			this->L = L;
			this->root = NULL;
			this->deltaT = 0;
			this->lastpointtime = 0;
		}

		double getDistance(Point *p, CF *cf)
		{
			// Get Euclidean distance of point from centroid of CF
			return sqrt(pow((p->x - (cf->lsx/cf->n)), 2) + pow((p->y - (cf->lsy/cf->n)), 2));
		}

		double getDistance(CF *cf1, CF *cf2)
		{
			double x1, y1, x2, y2;
			x1 = cf1->lsx / cf1->n;
			x2 = cf2->lsx / cf2->n;
			y1 = cf1->lsy / cf1->n;
			y2 = cf2->lsy / cf2->n;
			return sqrt(pow(x1-x2, 2) + pow(y1-y2,2));
		}
		
		bool needsSplit(Node *node)
		{
			if (node->size <= M)
				return false;
			return true;
		}
		
		bool checkedall(vector<int> &positions)
		{
			for(int i = (M+1)/2 - 1, j = M; i >=	0; i--, j--)
				if(positions[i] != j)
					return false;
			return true;
		}
		
		double getIntraGroupDistance(Node *node, vector<int> &group1, vector<int> &group2)
		{
			double distance = 0.0;
			for(int i = 0; i < group1.size(); i++)
				for(int j = i+1; j < group1.size(); j++)
					distance += getDistance(node->cf[group1[i]], node->cf[group1[j]]);
			for(int i = 0; i < group2.size(); i++)
				for(int j = i+1; j < group2.size(); j++)
					distance += getDistance(node->cf[group2[i]], node->cf[group2[j]]);
			return distance;
		}

		void getNextPosition(vector<int> &positions)
		{
			int i, j;
			for(i = positions.size()-1, j = M; i >= 0; i--, j--)
			{
				if(positions[i] < j)
					break;
			}
			positions[i]++;
			for(int k = i+1; k < positions.size(); k++)
				positions[k] = positions[k-1]+1;
		}

		void split(Node *node, int parentCFpos, int newtimestamp)
		{
			//cout<<"Splitting "<<node->getCF()->lsx<<" "<<node->getCF()->lsy<<"\n";
			vector<int> bestgroup1, bestgroup2;
			vector<int> group1, group2;
			vector<int> positions((M+1)/2);
			for(int i = 0; i < (M+1)/2; i++)
				positions[i] = i;
			group1 = positions;
			for(int i = 0; i < M+1; i++)
				if(find(positions.begin(), positions.end(), i) == positions.end())
					group2.push_back(i);
			bestgroup1 = group1;
			bestgroup2 = group2;
			double mindist = getIntraGroupDistance(node, group1, group2);
			do
			{
				getNextPosition(positions);
				group1 = positions;
				group2 = vector<int>();
				for(int i = 0; i < M+1; i++)
					if(find(positions.begin(), positions.end(), i) == positions.end())
						group2.push_back(i);
				double tempdist = getIntraGroupDistance(node, group1, group2);
				if (tempdist < mindist)
				{
					bestgroup1 = group1;
					bestgroup2 = group2;
					mindist = tempdist;
				}
			} while(!checkedall(positions));
			
			// Create new nodes to accomodate the two groups of cluster features
			Node *n1 = NULL, *n2 = NULL;

			if (node->parent == NULL)
			{
				// At root node, so split should create new root node
				this->root = new Node(NULL, m, M, l, L, false);
				n1 = new Node(this->root, m, M, l, L, node->isleaf);
				n2 = new Node(this->root, m, M, l, L, node->isleaf);
				for(int i = 0; i < bestgroup1.size(); i++)
				{
					n1->addCF(node->cf[bestgroup1[i]], node->child[bestgroup1[i]]);
					if (node->child[bestgroup1[i]])
						node->child[bestgroup1[i]]->parent = n1;
				}
				for(int i = 0; i < bestgroup2.size(); i++)
				{
					n2->addCF(node->cf[bestgroup2[i]], node->child[bestgroup2[i]]);
					if (node->child[bestgroup2[i]])
						node->child[bestgroup2[i]]->parent = n2;
				}
				this->root->addCF(n1->getCF(newtimestamp), n1);
				this->root->addCF(n2->getCF(newtimestamp), n2);
				delete node;
			}
			else
			{
				// Node could be internal or leaf node
				n1 = new Node(node->parent, m, M, l, L, node->isleaf);
				n2 = new Node(node->parent, m, M, l, L, node->isleaf);
				for(int i = 0; i < bestgroup1.size(); i++)
				{
					n1->addCF(node->cf[bestgroup1[i]], node->child[bestgroup1[i]]);
					if (node->child[bestgroup1[i]])
						node->child[bestgroup1[i]]->parent = n1;
				}
				for(int i = 0; i < bestgroup2.size(); i++)
				{
					n2->addCF(node->cf[bestgroup2[i]], node->child[bestgroup2[i]]);
					if (node->child[bestgroup2[i]])
						node->child[bestgroup2[i]]->parent = n2;
				}
				CF *cftodelete = node->parent->cf[parentCFpos];
				node->parent->cf[parentCFpos] = n1->getCF(newtimestamp);
				node->parent->child[parentCFpos] = n1;
				node->parent->addCF(n2->getCF(newtimestamp), n2);
				delete cftodelete;
				delete node;
			}
		}
		
		CF* insert(Point *p, Node *curnode, int whichCFofParent, int newtimestamp)
		{
			if (curnode->isleaf)
			{
				double minimum = curnode->cf[0]->n;
				int minpos = 0;
				for(int i = 1; i < curnode->size; i++)
				{	
					curnode->cf[i]->update(newtimestamp);
					if (curnode->cf[i]->n < minimum)
					{
						minimum = curnode->cf[i]->n;
						minpos = i;
					}
				}
				if (minimum < pow(BETA, -LAMBDA * this->deltaT * NUMINSERTS) && curnode->size+1 > M)
				{
					//cout<<minimum<<" is less than "<<pow(BETA, -LAMBDA * this->deltaT * NUMINSERTS)<<"\n";
					CF *tempcf = curnode->cf[minpos];
					curnode->cf[minpos] = new CF(p->x, p->y, newtimestamp, 1);
					return tempcf;
				}
				else
				{
					curnode->addPoint(p);
					if (needsSplit(curnode))
						split(curnode, whichCFofParent, newtimestamp);
					return NULL;
				}
			}
			else
			{
				// Find closest cluster feature to recurse on while not a child
				for(int i = 0; i < curnode->size; i++)
					curnode->cf[i]->update(newtimestamp);
				double mindist = getDistance(p, curnode->cf[0]);
				int insertpos = 0;
				for (int i = 1; i < curnode->size; i++)
				{
					double tempdist = getDistance(p, curnode->cf[i]);
					if (tempdist < mindist)
					{
						mindist = tempdist;
						insertpos = i;
					}
				}
				// Update CFs on the path to the leaf
				// TODO: Update timestamp
				curnode->cf[insertpos]->lsx += p->x;
				curnode->cf[insertpos]->lsy += p->y;
				curnode->cf[insertpos]->ssx += p->x*p->x;
				curnode->cf[insertpos]->ssy += p->y*p->y;
				curnode->cf[insertpos]->n += 1;

				CF *tempCF = NULL;

				if ((tempCF = insert(p, curnode->child[insertpos], insertpos, newtimestamp)) == NULL)
				{
					if (needsSplit(curnode))
						split(curnode, whichCFofParent, newtimestamp);
					return NULL;
				}
				else
				{
					curnode->cf[insertpos]->lsx -= tempCF->lsx;
					curnode->cf[insertpos]->lsy -= tempCF->lsy;
					curnode->cf[insertpos]->ssx -= tempCF->ssx;
					curnode->cf[insertpos]->ssy -= tempCF->ssy;
					curnode->cf[insertpos]->n -= tempCF->n;
					return tempCF;
				}
			}
		}

		void insert(int x, int y, int newtimestamp)
		{
			points++;
			// TODO: update CF timestamps for features along path
			this->deltaT = (this->deltaT + newtimestamp - this->lastpointtime)/2;
			this->lastpointtime = newtimestamp;
			Point *p = new Point(x, y, newtimestamp);
			if (root == NULL)
			{
				root = new Node(NULL, m, M, l, L, true);
				root->addPoint(p);
			}
			else
			{
				Node *curnode = root;
				CF *tempCF = insert(p, curnode, curnode->size, newtimestamp);
				if (tempCF)
				{
					points--;
					delete tempCF;
				}
			}
		}

		void printTree(Node *node, int nodeno)
		{
			if (node == NULL)
				return;
			cout<<"At node "<<nodeno<<"\n";
			for(int i = 0; i < node->size; i++)
			{
				cout<<"CF "<<i<<": ";
				cout<<"n = "<<node->cf[i]->n<<" ";
				cout<<"lsx = "<<node->cf[i]->lsx<<" ";
				cout<<"lsy = "<<node->cf[i]->lsy<<" ";
				cout<<"ssx = "<<node->cf[i]->ssx<<" ";
				cout<<"ssy = "<<node->cf[i]->ssy<<"\n";
			}
			cout<<"\n";
			for(int i = 0; i < node->size; i++)
				printTree(node->child[i], ++nodeno);				
		}

		void printTree()
		{
			printTree(this->root, 0);	
		}

		void printPoints(Node *node)
		{
			if(node->isleaf)
			{
				for(int i = 0; i < node->size; i++)
					cout<<(node->cf[i]->lsx)/(node->cf[i]->n)<<" "<<(node->cf[i]->lsy)/(node->cf[i]->n)<<"\n";
			}
			else
			{
				for(int i = 0; i < node->size; i++)
					printPoints(node->child[i]);
			}
		}

		void printAllPoints()
		{
			printPoints(this->root);		
		}

		CF* getRootCF()
		{
			return this->root->getCF(this->lastpointtime);
		}

		vector<Node*> getNodes(int reqno)
		{
			vector<Node*> curnodes;
			int prevdepth = 0;
			queue<NI> Q;
			Q.push(NI(this->root, 0));
			while(!Q.empty())
			{
				NI temp = Q.front();
				Q.pop();
				int curdepth = temp.second;
				Node *curnode = temp.first;
				if (curdepth > prevdepth && curnodes.size() >= reqno)
					return curnodes;
				else if (curdepth > prevdepth && curnode->isleaf)
					return curnodes;
				else if (curdepth > prevdepth && !curnode->isleaf)
				{
					prevdepth = curdepth;
					curnodes.clear();
				}
				curnodes.push_back(curnode);
				for(int i = 0; i < curnode->size; i++)
				{
					if (curnode->child[i] != NULL)
						Q.push(NI(curnode->child[i], curdepth+1));
				}
			}
			return curnodes;
		}

		void populateDescendants(Node *node, vector<II> &result)
		{
			if (node->isleaf)
			{
				for(int i = 0; i < node->size; i++)
					result.push_back(II((node->cf[i]->lsx) / (node->cf[i]->n), (node->cf[i]->lsy) / (node->cf[i]->n)));
			}
			else
			{
				for(int i = 0; i < node->size; i++)
					if(node->child[i] != NULL)
						populateDescendants(node->child[i], result);
			}
		}

		vector<II> getDescendantPoints(Node *node)
		{
			vector<II> result;
			populateDescendants(node, result);
			return result;
		}

};

double distance(Point p1, Point cluster, int done)
{
	return sqrt(pow(p1.x - (double)cluster.x/done,2) + pow(p1.y - (double)cluster.y/done, 2));
}

double distance(Point p, CF cf)
{
	return sqrt(pow(p.x - cf.lsx/cf.n, 2) + pow(p.y - cf.lsy/cf.n, 2));
}

int pathcompress(int *heads, int i)
{
	if (heads[i] == i)
		return i;
	else return heads[i] = pathcompress(heads, heads[heads[i]]);
}

double distance(int lsx1, int lsy1, int n1, int lsx2, int lsy2, int n2)
{
	return sqrt(pow(((double)lsx1)/n1 - ((double)lsx2)/n2, 2) + pow(((double)lsx1)/n1 - ((double)lsx2)/n2,2));
}

double sqrdistance(double x1, double y1, double x2, double y2)
{
	return ((x1-x2)*(x1-x2) + (y1-y2)*(y1-y2));
}

int main(int argc, char *argv[])
{
	srand(time(NULL));
	timeval t, tstart;
	gettimeofday(&tstart, NULL);
	
	int numTasks;
	int rank;

	MPI::Init(argc, argv);
	numTasks = MPI::COMM_WORLD.Get_size();
	rank = MPI::COMM_WORLD.Get_rank();
	
	if (rank == MASTER)
	{
		vector<Point> initialPoints[numTasks+1];
		double lsx[numTasks+1], lsy[numTasks+1], ssx[numTasks+1], ssy[numTasks+1];
		for(int j = 1; j < numTasks; j++)
		{
			for(int i = 0; i < INITIALSIZE; i++)
			{
				int tempx, tempy;
				gettimeofday(&t, NULL);
				cin>>tempx>>tempy;
				int timestamp = (t.tv_sec - tstart.tv_sec) * 1000 + (((double) (t.tv_usec - tstart.tv_usec)) / 1000);
				Point p = Point(tempx, tempy, timestamp);
				initialPoints[j].push_back(p);
				lsx[j] += tempx;
				lsy[j] += tempy;
				ssx[j] += tempx*tempx;
				ssy[j] += tempy*tempy;
			}
		}
		
		gettimeofday(&t, NULL);
		int timestamp = (t.tv_sec - tstart.tv_sec) * 1000 + (((double) (t.tv_usec - tstart.tv_usec)) / 1000);
		CF roots[numTasks+1];
		for(int i = 1; i < numTasks; i++)
			roots[i] = CF(INITIALSIZE, lsx[i], lsy[i], ssx[i], ssy[i], timestamp);
		
		int countsent[numTasks+1];
		for(int i = 1; i < numTasks; i++)
			countsent[i] = INITIALSIZE;
		double buffer[numTasks+1][20];
		double buffer1[20], buffer2[20];
		MPI::Request r[numTasks+1];
		
		// Write data to corresponding buffers
		for(int i = 1; i < numTasks; i++)
		{
			buffer[i][0] = initialPoints[i][0].x;
			buffer[i][1] = initialPoints[i][0].y;
		}
		
		for(int i = 1; i < numTasks; i++)
			r[i] = MPI::COMM_WORLD.Isend(buffer[i], 2, MPI::DOUBLE, i, 0);
		
		
		for(int j = 1; j < INITIALSIZE; j++)
		{
			for(int i = 1; i < numTasks; i++)
			{
				r[i].Wait();
				buffer[i][0] = initialPoints[i][j].x;
				buffer[i][1] = initialPoints[i][j].y;
				r[i] = MPI::COMM_WORLD.Isend(buffer[i], 2, MPI::DOUBLE, i, 0);
			}			
		}

		int starttime;
		bool first = true;
		int count = INITIALSIZE * (numTasks-1);
		int numclusters;
		do
		{
			int x, y;
			cin>>x>>y;
			if (x == SENTINEL)
			{
				numclusters = y;
				break;
			}
			gettimeofday(&t, NULL);
			int timestamp = (t.tv_sec - tstart.tv_sec) * 1000 + (((double) (t.tv_usec - tstart.tv_usec)) / 1000);
			if (first)
			{
				starttime = timestamp;
				first = false;
			}
			Point p(x, y, timestamp);
			for(int i = 1; i < numTasks; i++)
			{
				if (countsent[i] == UPDATECHECK)
				{
					r[i].Wait();
					MPI::COMM_WORLD.Recv(buffer[i], 5, MPI::DOUBLE, i, 0);
					roots[i] = CF(buffer[i][0], buffer[i][1], buffer[i][2], buffer[i][3], buffer[i][4], timestamp);
					countsent[i] = 0;
				}
			}
			int minindex = 1;
			double mindist = distance(p, roots[1]);
			for(int i = 2; i < numTasks; i++)
			{
				if(distance(p, roots[i]) < mindist)
				{
					minindex = i;
					mindist = distance(p, roots[i]);
				}
			}
			r[minindex].Wait();
			buffer[minindex][0] = p.x;
			buffer[minindex][1] = p.y;
			r[minindex] = MPI::COMM_WORLD.Isend(buffer[minindex], 2, MPI::DOUBLE, minindex, 0);
			countsent[minindex]++;

		} while(true);
		
		double tempbuffers[numTasks+1][50];
		for(int i = 1; i < numTasks; i++)
		{
			if (countsent[i] == UPDATECHECK)
				MPI::COMM_WORLD.Recv(tempbuffers[i], 5, MPI::DOUBLE, i, 0);
		}

		buffer[0][0] = SENTINEL;
		buffer[0][1] = SENTINEL;
		buffer[0][2] = SENTINEL;
		for(int i = 1; i < numTasks; i++)
			MPI::COMM_WORLD.Send(buffer[0], 2, MPI::DOUBLE, i, 0);
		
		// Request Clustering
		int torequest = (int) ceil(numclusters / numTasks);
		int tempbuffer[10];
		tempbuffer[0] = torequest;
		int numofclusters[numTasks+1];
		for(int i = 1; i < numTasks; i++)
		{
			MPI::COMM_WORLD.Send(tempbuffer, 1, MPI::INT, i, 0);
			MPI::COMM_WORLD.Recv(tempbuffer, 1, MPI::INT, i, 0);
			numofclusters[i] = tempbuffer[0];
		}
		int totalclusters = 0;
		for(int i = 1; i < numTasks; i++)
			totalclusters += numofclusters[i];

		vector<II> clusters[totalclusters];
		int curcluster = 0;

		for(int i = 1; i < numTasks; i++)
		{
			for(int j = 0; j < numofclusters[i]; j++)
			{
				tempbuffer[0] = j;
				MPI::COMM_WORLD.Send(tempbuffer, 1, MPI::INT, i, 0);
				MPI::COMM_WORLD.Recv(tempbuffer, 2, MPI::INT, i, 0);
				int numpoints = tempbuffer[1];
				for(int k = 0; k < numpoints; k++)
				{
					MPI::COMM_WORLD.Recv(tempbuffer, 2, MPI::INT, i, 0);
					clusters[curcluster].push_back(II(tempbuffer[0], tempbuffer[1]));
				}
				curcluster++;
			}
		}
		
		// Received all clusters at this point
		// Should merge till we have required number of clusters.

		int heads[totalclusters];
		int lsxs[totalclusters], lsys[totalclusters], pointcounts[totalclusters];

		for(int i = 0; i < totalclusters; i++)
		{
			heads[i] = i;
			pointcounts[i] = clusters[i].size();
			int lx = 0, ly = 0; 
			for(int j = 0; j < clusters[i].size(); j++)
			{
				lx += clusters[i][j].first;
				ly += clusters[i][j].second;
			}
			lsxs[i] = lx;
			lsys[i] = ly;
		}
		
		int clustercount = totalclusters;
		while(clustercount > numclusters)
		{
			int mini, minj;
			double mindist;
			bool first = true;
			for(int i = 0; i < totalclusters; i++)
			{
				heads[i] = pathcompress(heads, heads[i]);
				for(int j = i+1; j < totalclusters; j++)
				{
					heads[j] = pathcompress(heads, heads[j]);
					if (heads[i] == heads[j]) continue;
					if (first)
					{
						first = false;
						mini = i;
						minj = j;
						mindist = distance(lsxs[heads[i]], lsys[heads[i]], pointcounts[heads[i]], lsxs[heads[j]], lsys[heads[j]], pointcounts[heads[j]]);
					}
					else if (distance(lsxs[heads[i]], lsys[heads[i]], pointcounts[heads[i]], lsxs[heads[j]], lsys[heads[j]], pointcounts[heads[j]]) < mindist)
					{
						mindist = distance(lsxs[heads[i]], lsys[heads[i]], pointcounts[heads[i]], lsxs[heads[j]], lsys[heads[j]], pointcounts[heads[j]]);
						mini = i;
						minj = j;
					}
				}
			}
			
			// mini and minj are the clusters to be merged based on Euclidean distance
			pointcounts[heads[mini]] += pointcounts[heads[minj]];
			lsxs[heads[mini]] += lsxs[heads[minj]];
			lsys[heads[mini]] += lsys[heads[minj]];
			heads[heads[minj]] = heads[mini];
			heads[minj] = pathcompress(heads, heads[minj]);
			clustercount--;
		}

		int newclustersnums[totalclusters];
		int newnum = 1;
		for(int i = 0; i < totalclusters; i++)
			newclustersnums[i] = -1;
		
		for(int i = 0; i < totalclusters; i++)
			if (newclustersnums[heads[i]] == -1)
				newclustersnums[heads[i]] = newnum++;
		
		/*for(int i = 0; i < totalclusters; i++)
			for(int j = 0; j < clusters[i].size(); j++)
				cout<<clusters[i][j].first<<" "<<clusters[i][j].second<<" "<<newclustersnums[heads[i]]<<"\n"; */

		double sse = 0.0;
		double centerx[newnum], centery[newnum];
		int clusterns[newnum];
		for(int i = 0; i < newnum; i++)
		{
			centerx[i] = centery[i] = 0.0;
			clusterns[i] = 0;
		}
		for(int i = 0; i < totalclusters; i++)
		{
			for(int j = 0; j < clusters[i].size(); j++)
			{	
				centerx[newclustersnums[heads[i]]] += clusters[i][j].first;
				centery[newclustersnums[heads[i]]] += clusters[i][j].second;
				clusterns[newclustersnums[heads[i]]]++;
			}
		}
		for(int i = 1; i < newnum; i++)
		{
			centerx[i] /= clusterns[i];
			centery[i] /= clusterns[i];
		}

		for(int i = 0; i < totalclusters; i++)
			for(int j = 0; j < clusters[i].size(); j++)
				sse += sqrdistance(clusters[i][j].first, centerx[newclustersnums[heads[i]]], clusters[i][j].second, centery[newclustersnums[heads[i]]]);

		cout<<fixed<<sse;

		//gettimeofday(&t, NULL);
		//timestamp = (t.tv_sec - tstart.tv_sec) * 1000 + (((double) (t.tv_usec - tstart.tv_usec)) / 1000);
		//int endtime = timestamp;
		//cout<<endtime - starttime<<"\n";
		// End master node's work
	}
	else 
	{
		double buffer[20];
		int countreceived = 0;
		ClusTree T(1, 3, 1, 3);
		do
		{
			//cout<<rank<<"\n";
			MPI::COMM_WORLD.Recv(buffer, 2, MPI::DOUBLE, 0, 0);
			double x = buffer[0], y = buffer[1];
			//cout<<"Slave "<<rank<<" received "<<x<<" "<<y<<" "<<timestamp<<"\n";
			if (x < (SENTINEL+100))
				break;
			gettimeofday(&t, NULL);
			int timestamp = (t.tv_sec - tstart.tv_sec) * 1000 + (((double) (t.tv_usec - tstart.tv_usec)) / 1000);
			T.insert(x, y, timestamp);
			countreceived++;
			if (countreceived == UPDATECHECK)
			{
				countreceived = 0;
				CF *tempCF = T.getRootCF();
				buffer[0] = tempCF->n;
				buffer[1] = tempCF->lsx;
				buffer[2] = tempCF->lsy;
				buffer[3] = tempCF->ssx;
				buffer[4] = tempCF->ssy;
				delete tempCF;
				MPI::COMM_WORLD.Send(buffer, 5, MPI::DOUBLE, 0, 0);
			}
		} while (true);
		
		int tempbuffer[10];
		MPI::COMM_WORLD.Recv(tempbuffer, 1, MPI::INT, 0, 0);
		int numclusters = tempbuffer[0];
		
		vector<Node*> nodes = T.getNodes(numclusters);
		vector<II> clusters[nodes.size()];
		for(int i = 0; i < nodes.size(); i++)
			clusters[i] = T.getDescendantPoints(nodes[i]);
		
		tempbuffer[0] = nodes.size();
		MPI::COMM_WORLD.Send(tempbuffer, 1, MPI::INT, 0, 0);

		for(int i = 0; i < nodes.size(); i++)
		{
			MPI::COMM_WORLD.Recv(tempbuffer, 1, MPI::INT, 0, 0);
			tempbuffer[1] = clusters[tempbuffer[0]].size();
			MPI::COMM_WORLD.Send(tempbuffer, 2, MPI::INT, 0, 0);
			int clusternum = tempbuffer[0];
			for(int j = 0; j < clusters[clusternum].size(); j++)
			{
				tempbuffer[0] = clusters[clusternum][j].first;
				tempbuffer[1] = clusters[clusternum][j].second;
				MPI::COMM_WORLD.Send(tempbuffer, 2, MPI::INT, 0, 0);
			}
		}

		// End slave code
	}
	
	MPI::Finalize();
	return 0;		
}
